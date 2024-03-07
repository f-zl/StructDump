
#include <cstdlib>
#include <iostream>
// #include <llvm/MC/MCRegisterInfo.h>
// #include <llvm/MC/TargetRegistry.h>
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

// static void error(Error Err) {
//   if (!Err)
//     return;
//   WithColor::error() << toString(std::move(Err)) << "\n";
//   exit(1);
// }
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
static bool handleBuffer(StringRef Filename, MemoryBufferRef Buffer,
                         HandlerFn HandleObj, StringRef VariableName,
                         raw_ostream &OS) {
  Expected<std::unique_ptr<Binary>> BinOrErr = object::createBinary(Buffer);
  error(Filename, BinOrErr.takeError());
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
      if (!HandleObj(*Obj, *DICtx, Filename, VariableName, OS))
        Result = false;
    }
  } // handle Marh-O. removed
  else if (auto *Arch = dyn_cast<Archive>(BinOrErr->get()))
    Result = handleArchive(Filename, *Arch, HandleObj, VariableName, OS);
  return Result;
}
static bool handleFile(StringRef Filename, HandlerFn HandleObj,
                       StringRef VariableName, raw_ostream &OS) {
  ErrorOr<std::unique_ptr<MemoryBuffer>> BuffOrErr =
      MemoryBuffer::getFileOrSTDIN(Filename);
  error(Filename, BuffOrErr.getError());
  std::unique_ptr<MemoryBuffer> Buffer = std::move(BuffOrErr.get());
  return handleBuffer(Filename, *Buffer, HandleObj, VariableName, OS);
}

static bool IsStructType(Type type) {
  return type.getTag() == DW_TAG_structure_type;
}
static std::optional<uint64_t>
GetOffsetOfType(Variable variable) { // FIXME 对于DWARF4，offset不对
  auto type = variable.find(DW_AT_type);
  assert(type);
  auto &typeValue = type.value();
  //   typeValue.dump(outs()); // getForm()=DW_FORM_ref4,
  //   dump()显示是type的offset值
  // return typeValue.getAsCStringOffset();
  // 参照DWARFFormValue::dump
  return typeValue.getRawUValue() + typeValue.getUnit()->getOffset();
}
static Type FindVariableType(Variable variable) {
  auto typeOffset = GetOffsetOfType(variable);
  assert(typeOffset.has_value());
  outs() << format("typeOffset=%x\n", typeOffset.value());
  auto unit = variable.getDwarfUnit();
  auto typeDie = unit->getDIEForOffset(typeOffset.value());
  assert(typeDie.isValid());
  auto typeName = typeDie.find(DW_AT_name);
  if (typeName.has_value()) { // typedef有名，struct tag没名
    outs() << formatv("type is {0}\n", typeName->getAsCString().get());
  }
  // 循环typedef直到找到具体类型
  while (typeDie.getTag() == DW_TAG_typedef) {
    auto type = typeDie.find(DW_AT_type);
    assert(type.has_value());
    auto offset = type.value().getRawUValue();
    typeDie = unit->getDIEForOffset(offset + type->getUnit()->getOffset());
  }
  return typeDie;
}
static bool VariableNameMatch(DWARFDie &die, StringRef expectName) {
  if (die.getTag() != DW_TAG_variable)
    return false;
  //   auto actualName =
  //   die.getAttributeValueAsReferencedDie(dwarf::DW_AT_name);
  auto name = die.find(dwarf::DW_AT_name);
  // 有的variable没有name
  if (!name.has_value())
    return false;
  auto actualName = name.value().getAsCString();
  assert(actualName);
  return actualName.get() == expectName;
  //   for (const auto &attr : die.attributes()) {
  //     if (attr.Attr == DW_AT_name) {
  //       auto actualName = attr.Value.getAsCString();
  //       assert(actualName);
  //       if (expectName == actualName.get()) {
  //         return true;
  //       } else {
  //         return false; // 1个die只有1个DW_AT_name
  //       }
  //     }
  //   }
  //   return false;
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
struct DwarfTagArrayType { // DW_TAG_array_type
  DWARFDie die;
  DwarfTagArrayType(DWARFDie die) : die{die} {
    assert(die.getTag() == DW_TAG_array_type);
  }
  // DWARF5标准说array_type一定有DW_AT_name，但dump出来好像没有
  DWARFDie ElementType() const { // array一定有type
    auto type = die.find(DW_AT_type);
    auto offset = type->getRawUValue() + type->getUnit()->getOffset();
    return die.getDwarfUnit()->getDIEForOffset(offset);
  }
  size_t Length() const {
    for (auto child = die.getFirstChild(); child; child = child.getSibling()) {
      switch (child.getTag()) {    // 长度在subrange或enumeration
      case DW_TAG_subrange_type: { // gcc用的是这个
        auto value = child.find(DW_AT_upper_bound).value();
        return value.getAsUnsignedConstant().value() + 1;
      } break;
      case DW_TAG_enumeration_type:
        // TODO
        break;
      default:
        break;
      }
    }
    assert(0);
  }
};
struct DwarfTagMember { // DW_TAG_member
  DWARFDie die;
  DwarfTagMember(DWARFDie die) : die{die} {
    assert(die.getTag() == DW_TAG_member);
  }
  const char *Name() const { // member一定有name，除非是匿名union
    return die.find(DW_AT_name)->getAsCString().get();
  }
  DWARFDie Type() const { // member一定有type
    auto offset = die.find(DW_AT_type)->getAsCStringOffset().value();
    return die.getDwarfUnit()->getDIEForOffset(offset);
  }
  size_t MemberOffset() const { // output of offsetof()
                                // TODO 可能没有data_member_location
    return die.find(DW_AT_data_member_location)
        ->getAsUnsignedConstant()
        .value();
  }
};

static void PrintAttrTypeName(DWARFDie die, raw_ostream &os) {
  auto offset = die.find(DW_AT_type).value().getRawUValue();
  auto unit = die.getDwarfUnit();
  auto typeDie = unit->getDIEForOffset(offset);
  switch (typeDie.getTag()) {
  case DW_TAG_typedef:
  case DW_TAG_base_type:
    // 打印DW_AT_name
    os << typeDie.find(DW_AT_name)->getAsCString().get();
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
  // union
  default:
    os << "?";
  }
  // auto typeName = typeDie.find(DW_AT_name);
  // // return typeName.value().getAsCString().get();
  // if (typeName) {
  //   return typeName.value().getAsCString().get();
  // } else {
  //   return "?";
  // }
}
static void ProcessTypeNonRecursive(Type type, raw_ostream &os,
                                    unsigned childLv) {
  // type.dump();
  while (childLv-- > 0) {
    os << '-';
  }
  switch (type.getTag()) {
  case DW_TAG_member:
    // 可以有DW_AT_data_member_location或DW_AT_data_bit_offset或没有
    os << formatv("member name {0} type ",
                  type.find(DW_AT_name).value().getAsCString().get());
    PrintAttrTypeName(type, os);
    os << formatv(" offset {0}", DwarfTagMember{type}.MemberOffset());
    os << '\n';
    break;
  case DW_TAG_structure_type:
    os << formatv(
        "struct size {0}\n",
        type.find(DW_AT_byte_size).value().getAsUnsignedConstant().value());
    // os << formatv("struct name {0} type {1}\n",
    //               type.find(DW_AT_name).value().getAsCString().get(),
    //               type.find(DW_AT_type).value().getAsCString().get());
    break;
  default:
    os << formatv("Unknown tag {0}\n", type.getTag());
  }
}
static void ProcessType(Type type, raw_ostream &os, unsigned childLv);
static void ProcessStruct(Type type, raw_ostream &os, unsigned childLv) {
  // 打印名字
  // 从第一个child开始循环，直到null
  os << formatv(
      "struct size {0}\n",
      type.find(DW_AT_byte_size).value().getAsUnsignedConstant().value());
  for (auto child = type.getFirstChild(); child; child = child.getSibling()) {
    ProcessType(child, os, childLv + 1);
  }
}
static void PrintDW_AT_type(DWARFDie Die, DWARFFormValue FormValue,
                            raw_ostream &OS) {
  DWARFDie D = resolveReferencedType(Die, FormValue);
  OS << "\"";
  dumpTypeQualifiedName(D, OS); // 如果DW_AT_type是数组，可以打印出数组长度
  OS << "\"";
}
static void ProcessMember(Type type, raw_ostream &os, unsigned childLv) {
  // 可以有DW_AT_data_member_location或DW_AT_data_bit_offset或没有
  os << formatv("member name {0} type ",
                type.find(DW_AT_name).value().getAsCString().get());
  PrintAttrTypeName(type, os);
  // PrintDW_AT_type(type, type.find(DW_AT_type).value(), os);
  auto member = DwarfTagMember{type};
  os << formatv(" offset {0}\n", member.MemberOffset());
  ProcessType(member.Type(), os, childLv + 1);
}
static DWARFDie GetDW_AT_Type(DWARFDie die) {
  auto originalType = die.find(DW_AT_type).value();
  auto unit = die.getDwarfUnit();
  auto originalTypeOffset = originalType.getRawUValue() + unit->getOffset();
  return unit->getDIEForOffset(originalTypeOffset);
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
  for (unsigned lv = 0; lv < childLv; lv++) {
    os << '-';
  }
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
                           const Twine &Filename, StringRef VariableName,
                           raw_ostream &OS) {
  (void)Obj;
  (void)Filename;
  auto variable = FindVariable(DICtx, VariableName);
  if (!variable.isValid()) {
    OS << "variable not found\n";
    exit(1);
  }
  auto type = FindVariableType(variable);
  if (!IsStructType(type)) {
    std::cerr << "variable type is not a struct\n";
    exit(EXIT_FAILURE);
  }
  ProcessType(type, outs(), 0);
  return false;
}
static void Process(StringRef fileName, StringRef variableName) {
  handleFile(fileName, dumpObjectFile, variableName, llvm::outs());
}
// DWARF4处理不正确 (llvm-dwarfdump则可以处理)
// 可能不是DWARF4，而是多个Compilation Unit的影响
int main(int argc, char **argv) {
  if (argc != 3) {
    std::cout << "usage: StructDump <elf> <variable>\n";
    exit(EXIT_FAILURE);
  }
  llvm::InitLLVM X(argc, argv);
  errs().tie(&outs());
  llvm::InitializeAllTargetInfos();
  llvm::InitializeAllTargetMCs();
  Process(argv[1], argv[2]);
}
