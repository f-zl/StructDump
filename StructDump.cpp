#include "DwarfTag.hpp"
#include <cstdlib>
#include <iostream>
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/DebugInfo/DWARF/DWARFObject.h>
#include <llvm/Object/Archive.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/InitLLVM.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_ostream.h>
using namespace llvm;
using namespace llvm::dwarf;
using namespace llvm::object;

using Variable = DWARFDie;
using Type = DWARFDie;
using HandlerFn = std::function<bool(ObjectFile &, DWARFContext &DICtx,
                                     const Twine &, StringRef, raw_ostream &)>;

static void error(StringRef Prefix, Error Err) {
  if (!Err)
    return;
  WithColor::error() << Prefix << ": " << toString(std::move(Err)) << "\n";
  exit(1);
}
static void error(StringRef Prefix, std::error_code EC) {
  error(Prefix, errorCodeToError(EC));
}
static DWARFDie resolveReferencedType(DWARFDie D, DWARFFormValue F) {
  return D.getAttributeValueAsReferencedDie(F).resolveTypeUnitReference();
}
static bool handleBuffer(StringRef Filename, MemoryBufferRef Buffer,
                         HandlerFn HandleObj, StringRef VariableName,
                         raw_ostream &OS);
static bool handleArchive(StringRef Filename, Archive &Arch,
                          HandlerFn HandleObj, StringRef VariableName,
                          raw_ostream &OS) {
  bool Result = true;
  Error Err = Error::success();
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
static bool filterArch(ObjectFile &Obj) {
  (void)Obj;
  return true;
}
static bool handleBuffer(StringRef FileName, MemoryBufferRef Buffer,
                         HandlerFn HandleObj, StringRef VariableName,
                         raw_ostream &OS) {
  Expected<std::unique_ptr<Binary>> BinOrErr = object::createBinary(Buffer);
  error(FileName, BinOrErr.takeError());
  bool Result = true;
  auto RecoverableErrorHandler = [&](Error E) {
    Result = false;
    WithColor::defaultErrorHandler(std::move(E));
  };
  if (auto *Obj = dyn_cast<ObjectFile>(BinOrErr->get())) {
    if (filterArch(*Obj)) {
      std::unique_ptr<DWARFContext> DICtx = DWARFContext::create(
          *Obj, DWARFContext::ProcessDebugRelocations::Process, nullptr, "",
          RecoverableErrorHandler);
      bool ManuallyGenerateUnitIndex = false;
      DICtx->setParseCUTUIndexManually(ManuallyGenerateUnitIndex);
      if (!HandleObj(*Obj, *DICtx, FileName, VariableName, OS))
        Result = false;
    }
  } // handle Marh-O. removed
  else if (auto *Arch = dyn_cast<Archive>(BinOrErr->get()))
    Result = handleArchive(FileName, *Arch, HandleObj, VariableName, OS);
  return Result;
}
static bool handleFile(StringRef FileName, HandlerFn HandleObj,
                       StringRef VariableName, raw_ostream &OS) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BuffOrErr =
      MemoryBuffer::getFileOrSTDIN(FileName);
  error(FileName, BuffOrErr.getError());
  std::unique_ptr<MemoryBuffer> Buffer = std::move(BuffOrErr.get());
  return handleBuffer(FileName, *Buffer, HandleObj, VariableName, OS);
}

static bool IsStructType(Type type) {
  return type.getTag() == DW_TAG_structure_type;
}
static uint64_t GetOffset(const DWARFFormValue &formValue) {
  return formValue.getRawUValue() + formValue.getUnit()->getOffset();
}
static uint64_t GetOffsetOfAttrType(Variable variable) {
  auto type = variable.find(DW_AT_type);
  // getForm()=DW_FORM_ref4
  // 参照DWARFFormValue::dump
  return GetOffset(type.value());
}
static Type FindVariableType(Variable variable, raw_ostream &os) {
  auto typeOffset = GetOffsetOfAttrType(variable);
  os << format("typeOffset=%x\n", typeOffset);
  auto unit = variable.getDwarfUnit();
  auto typeDie = unit->getDIEForOffset(typeOffset);
  assert(typeDie.isValid());
  auto typeName = typeDie.find(DW_AT_name);
  if (typeName.has_value()) { // typedef有名，struct tag没名
    os << formatv("type is {0}\n", typeName->getAsCString().get());
  }
  // 循环typedef直到找到具体类型
  while (typeDie.getTag() == DW_TAG_typedef) {
    auto offset = GetOffsetOfAttrType(typeDie);
    typeDie =
        unit->getDIEForOffset(offset + typeDie.getDwarfUnit()->getOffset());
  }
  return typeDie;
}
static bool VariableNameMatch(DWARFDie &die, StringRef expectName) {
  if (die.getTag() != DW_TAG_variable)
    return false;
  auto name = die.find(dwarf::DW_AT_name);
  // 有的variable没有name
  if (!name.has_value())
    return false;
  auto actualName = name.value().getAsCString();
  assert(actualName);
  return actualName.get() == expectName;
}
static Variable FindVariable(DWARFContext &DICtx, StringRef name) {
  DWARFContext::unit_iterator_range Units = DICtx.info_section_units();
  for (const auto &U : Units) {
    DWARFDie rootDie = U->getUnitDIE(false);
    for (auto child = rootDie.getFirstChild(); child;
         child = child.getSibling()) {
      if (VariableNameMatch(child, name)) {
        return child;
      }
    }
  }
  return {};
}
static void PrintAttrTypeName(DWARFDie die, raw_ostream &os) {
  auto offset = die.find(DW_AT_type).value().getRawUValue();
  auto unit = die.getDwarfUnit();
  auto typeDie = unit->getDIEForOffset(offset + unit->getOffset());
  switch (typeDie.getTag()) {
  case DW_TAG_typedef:
  case DW_TAG_base_type:
    // 打印DW_AT_name
    os << GetDW_AT_name(typeDie);
    break;
  case DW_TAG_array_type: { // 打印基本属性
    // TODO 数组长度
    auto array = DwarfTagArrayType(typeDie);
    os << formatv("array of {0} length {1}",
                  array.ElementType().find(DW_AT_name)->getAsCString().get(),
                  array.Length());
  } break;
  case DW_TAG_structure_type:
    // TODO 可能有tag name，测试看看
    os << "struct";
    break;
  case DW_TAG_union_type:
  default:
    os << "?";
  }
}
static void ProcessType(Type type, raw_ostream &os, unsigned childLv);
static void ProcessStruct(Type type, raw_ostream &os, unsigned childLv) {
  DwarfTagStructureType st{type};
  os << formatv("struct size {0}\n", st.ByteSize());
  // 循环直到DW_TAG_null
  for (auto child = type.getFirstChild(); child; child = child.getSibling()) {
    ProcessType(child, os, childLv + 1);
  }
}
static void PrintDW_AT_type(DWARFDie Die, DWARFFormValue FormValue,
                            raw_ostream &OS) {
  DWARFDie D = resolveReferencedType(Die, FormValue);
  dumpTypeQualifiedName(D, OS); // 如果DW_AT_type是数组，可以打印出数组长度
}
static void ProcessMember(Type type, raw_ostream &os, unsigned childLv) {
  auto member = DwarfTagMember{type};
  // 可以有DW_AT_data_member_location或DW_AT_data_bit_offset或没有
  os << formatv("member name {0} type ", member.Name());
  PrintAttrTypeName(type, os);
  os << formatv(" offset {0}\n", member.MemberOffset());
  ProcessType(member.Type(), os, childLv + 1);
}
static DWARFDie GetDW_AT_Type(DWARFDie die) {
  auto originalType = die.find(DW_AT_type).value();
  auto unit = die.getDwarfUnit();
  auto originalTypeOffset = originalType.getRawUValue() + unit->getOffset();
  return unit->getDIEForOffset(originalTypeOffset);
}
static void PrintIndentLevel(raw_ostream &os, unsigned indentLevel) {
  for (unsigned lv = 0; lv < indentLevel; lv++) {
    os << '-';
  }
}
static void ProcessTypedef(Type type, raw_ostream &os, unsigned childLv) {
  // PrintDW_AT_type(type, type.find(DW_AT_type).value(), os);
  // 一直找，直到找到不是typedef
  os << formatv("typedef: {0}", type.find(DW_AT_name)->getAsCString().get());
  type = GetDW_AT_Type(type);
  while (type.getTag() == DW_TAG_typedef) {
    os << formatv(" -> {0}", type.find(DW_AT_name)->getAsCString().get());
    type = GetDW_AT_Type(type);
  }
  os << '\n';
  // 最终的tag可能是base_type, structure
  switch (type.getTag()) {
  case DW_TAG_structure_type:
    PrintIndentLevel(os, childLv + 1);
    ProcessStruct(type, os, childLv + 1);
    break;
  case DW_TAG_union_type:
  default:
    break;
  }
}
static void ProcessArrayType(Type type, raw_ostream &os, unsigned childLv) {
  // TODO 长度
  // DW_TAG_array_tag一般有DW_TAG_subrange_type作为child，后者有DW_AT_upper_bound是数组索引上限，长度=索引上限+1
  // PrintDW_AT_type(type, type.find(DW_AT_type).value(), os);
  os << formatv("array of {0}\n", DwarfTagArrayType(type)
                                      .ElementType()
                                      .find(DW_AT_name)
                                      ->getAsCString()
                                      .get());
}
static void ProcessType(Type type, raw_ostream &os, unsigned childLv) {
  // 递归处理struct类型
  PrintIndentLevel(os, childLv);
  switch (type.getTag()) {
  case DW_TAG_structure_type:
    ProcessStruct(type, os, childLv);
    break;
  case DW_TAG_member:
    ProcessMember(type, os, childLv);
    break;
  case DW_TAG_typedef:
    ProcessTypedef(type, os, childLv);
    break;
  case DW_TAG_base_type:
    os << formatv("base type {0}\n",
                  type.find(DW_AT_name)->getAsCString().get());
    break;
  case DW_TAG_array_type:
    ProcessArrayType(type, os, childLv);
    break;
  case DW_TAG_null:
    os << "tag null\n";
    break;
  default:
    os << formatv("Unknown tag {0}\n", type.getTag());
    break;
  }
}
static bool dumpObjectFile(ObjectFile &Obj, DWARFContext &DICtx,
                           const Twine &FileName, StringRef VariableName,
                           raw_ostream &OS) {
  (void)Obj;
  (void)FileName;
  auto variable = FindVariable(DICtx, VariableName);
  if (!variable.isValid()) {
    OS << "variable not found\n";
    exit(EXIT_FAILURE);
  }
  auto type = FindVariableType(variable, OS);
  if (!IsStructType(type)) {
    OS << "variable type is not a struct\n";
    exit(EXIT_FAILURE);
  }
  ProcessType(type, OS, 0);
  return false;
}
int main(int argc, char **argv) {
  if (argc != 3) {
    std::cout << "usage: StructDump <elf> <variable>\n";
    exit(EXIT_FAILURE);
  }
  llvm::InitLLVM X(argc, argv); // catch SIGABRT to print stacktrace
  errs().tie(&outs());
  StringRef FileName = argv[1];
  StringRef VariableName = argv[2];
  handleFile(FileName, dumpObjectFile, VariableName, llvm::outs());
}
