#pragma once
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/DebugInfo/DWARF/DWARFObject.h>
#include <llvm/Object/Archive.h>

using HandlerFn = std::function<bool(
    llvm::object::ObjectFile &, llvm::DWARFContext &DICtx, const llvm::Twine &,
    llvm::StringRef, llvm::raw_ostream &)>;

static void error(llvm::StringRef Prefix, llvm::Error Err) {
  if (!Err)
    return;
  llvm::WithColor::error() << Prefix << ": " << toString(std::move(Err))
                           << "\n";
  exit(1);
}
static void error(llvm::StringRef Prefix, std::error_code EC) {
  error(Prefix, llvm::errorCodeToError(EC));
}
static bool handleBuffer(llvm::StringRef Filename, llvm::MemoryBufferRef Buffer,
                         HandlerFn HandleObj, llvm::StringRef VariableName,
                         llvm::raw_ostream &OS);
static bool handleArchive(llvm::StringRef Filename, llvm::object::Archive &Arch,
                          HandlerFn HandleObj, llvm::StringRef VariableName,
                          llvm::raw_ostream &OS) {
  bool Result = true;
  llvm::Error Err = llvm::Error::success();
  for (const auto &Child : Arch.children(Err)) {
    auto BuffOrErr = Child.getMemoryBufferRef();
    error(Filename, BuffOrErr.takeError());
    auto NameOrErr = Child.getName();
    error(Filename, NameOrErr.takeError());
    std::string Name = (Filename + "(" + NameOrErr.get() + ")").str();
    Result &= handleBuffer(Name, BuffOrErr.get(), HandleObj, VariableName, OS);
  }
  error(Filename, std::move(Err));

  return Result;
}
/// Return true if the object file has not been filtered by an --arch option.
static bool filterArch(llvm::object::ObjectFile &Obj) {
  (void)Obj;
  return true;
}
static bool handleBuffer(llvm::StringRef FileName, llvm::MemoryBufferRef Buffer,
                         HandlerFn HandleObj, llvm::StringRef VariableName,
                         llvm::raw_ostream &OS) {
  llvm::Expected<std::unique_ptr<llvm::object::Binary>> BinOrErr =
      llvm::object::createBinary(Buffer);
  error(FileName, BinOrErr.takeError());
  bool Result = true;
  auto RecoverableErrorHandler = [&](llvm::Error E) {
    Result = false;
    llvm::WithColor::defaultErrorHandler(std::move(E));
  };
  if (auto *Obj = llvm::dyn_cast<llvm::object::ObjectFile>(BinOrErr->get())) {
    if (filterArch(*Obj)) {
      std::unique_ptr<llvm::DWARFContext> DICtx = llvm::DWARFContext::create(
          *Obj, llvm::DWARFContext::ProcessDebugRelocations::Process, nullptr,
          "", RecoverableErrorHandler);
      bool ManuallyGenerateUnitIndex = false;
      DICtx->setParseCUTUIndexManually(ManuallyGenerateUnitIndex);
      if (!HandleObj(*Obj, *DICtx, FileName, VariableName, OS))
        Result = false;
    }
  } // handle Marh-O. removed
  else if (auto *Arch = llvm::dyn_cast<llvm::object::Archive>(BinOrErr->get()))
    Result = handleArchive(FileName, *Arch, HandleObj, VariableName, OS);
  return Result;
}
static bool handleFile(llvm::StringRef FileName, HandlerFn HandleObj,
                       llvm::StringRef VariableName, llvm::raw_ostream &OS) {
  llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> BuffOrErr =
      llvm::MemoryBuffer::getFileOrSTDIN(FileName);
  error(FileName, BuffOrErr.getError());
  std::unique_ptr<llvm::MemoryBuffer> Buffer = std::move(BuffOrErr.get());
  return handleBuffer(FileName, *Buffer, HandleObj, VariableName, OS);
}