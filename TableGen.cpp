#include "BoilerPlate.hpp"
#include "DwarfTag.hpp"
#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/DebugInfo/DWARF/DWARFContext.h>
#include <llvm/DebugInfo/DWARF/DWARFObject.h>
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

static bool IsStructType(Type type) {
  return type.getTag() == DW_TAG_structure_type;
}
static Type FindVariableType(Variable variable, StringRef variableName,
                             raw_ostream &os) {
  auto typeDie = GetDW_AT_type(variable);
  assert(typeDie.isValid());
  auto typeName = typeDie.find(DW_AT_name);
  if (typeName.has_value()) { // typedef有名，struct tag没名
    os << formatv("type of {0} is {1}\n", variableName,
                  typeName->getAsCString().get());
  }
  // 循环typedef直到找到具体类型
  while (typeDie.getTag() == DW_TAG_typedef) {
    typeDie = GetDW_AT_type(typeDie);
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
// resolve DW_TAG_typedef to base type/array type/structure type
static DWARFDie ResolveTypedef(DWARFDie type) {
  while (type.getTag() == DW_TAG_typedef) {
    type = DwarfTagTypedef{type}.Type();
  }
  return type;
}
static void ProcessBaseType(DWARFDie originalType, DWARFDie resolvedType,
                            raw_ostream &os, uint64_t baseOffset) {
  DwarfTagBaseType bt{resolvedType};
  os << formatv("{0} offset {1} size {2}\n", GetDW_AT_name(originalType),
                baseOffset, bt.ByteSize());
}
static void ProcessArrayType(Type type, raw_ostream &os, uint64_t baseOffset) {
  DwarfTagArrayType array{type};
  os << formatv("{0}[{1}] offset {2}\n", GetDW_AT_name(array.ElementType()),
                array.Length(), baseOffset);
}
static void ProcessStruct(Type type, raw_ostream &os, uint64_t baseOffset,
                          Twine prefix = "");
static void PrintPrefix(raw_ostream &os, Twine prefix,
                        const DwarfTagMember &member) {
  if (prefix.isTriviallyEmpty()) {
    os << formatv("{0}: ", member.Name());
  } else {
    os << formatv("{0}.{1}: ", prefix, member.Name());
  }
}
static void ProcessMember(DWARFDie die, raw_ostream &os, uint64_t baseOffset,
                          Twine prefix) {
  DwarfTagMember member{die};
  auto type = member.Type();
  auto resolvedType = ResolveTypedef(type);
  auto offset = baseOffset + member.MemberOffset();
  switch (resolvedType.getTag()) {
  case DW_TAG_base_type:
    PrintPrefix(os, prefix, member);
    ProcessBaseType(type, resolvedType, os, offset);
    break;
  case DW_TAG_array_type:
    PrintPrefix(os, prefix, member);
    ProcessArrayType(type, os, offset);
    break;
  case DW_TAG_structure_type: {
    ProcessStruct(resolvedType, os, offset,
                  prefix.concat(Twine{member.Name()}));
  } break;
  default:
    os << formatv("Unknown tag {0}", resolvedType.getTag());
  }
}
static void ProcessStruct(Type type, raw_ostream &os, uint64_t baseOffset,
                          Twine prefix) {
  DwarfTagStructureType st{type};
  // iterate until DW_TAG_null
  for (auto child = type.getFirstChild();
       child && child.getTag() != DW_TAG_null; child = child.getSibling()) {
    ProcessMember(child, os, baseOffset, prefix);
  }
}
static void ProcessType(Type type, raw_ostream &os) {
  os << R"(std::vector<ParamConfig> CreateParamConfig(){return std::vector<ParamConfig>{
)";
  ProcessStruct(type, os, 0);
  os << "};}\n";
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
  auto type = FindVariableType(variable, VariableName, OS);
  if (!IsStructType(type)) {
    OS << "variable type is not a struct\n";
    exit(EXIT_FAILURE);
  }
  ProcessType(type, OS);
  return false;
}
int main(int argc, char **argv) {
  if (argc != 3) {
    llvm::outs() << "usage: TableGen <elf> <variable>\n";
    exit(EXIT_FAILURE);
  }
  llvm::InitLLVM X(argc, argv); // catch SIGABRT to print stacktrace
  errs().tie(&outs());
  StringRef FileName = argv[1];
  StringRef VariableName = argv[2];
  handleFile(FileName, dumpObjectFile, VariableName, llvm::outs());
}
