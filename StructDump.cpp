#include "BoilerPlate.hpp"
#include "DwarfTag.hpp"
#include <cstdlib>
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

static DWARFDie resolveReferencedType(DWARFDie D, DWARFFormValue F) {
  return D.getAttributeValueAsReferencedDie(F).resolveTypeUnitReference();
}
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
static void ProcessType(Type type, raw_ostream &os, unsigned childLv);
static void ProcessStruct(Type type, raw_ostream &os, unsigned childLv) {
  DwarfTagStructureType st{type};
  os << formatv("struct {0} size {1}\n", st.TagName(), st.ByteSize());
  // iterate until DW_TAG_null
  for (auto child = type.getFirstChild();
       child && child.getTag() != DW_TAG_null; child = child.getSibling()) {
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
  os << formatv("member name {0} offset {1}\n", member.Name(),
                member.MemberOffset());
  ProcessType(member.Type(), os, childLv + 1);
}
static void PrintIndentLevel(raw_ostream &os, unsigned indentLevel) {
  for (unsigned lv = 0; lv < indentLevel; lv++) {
    os << '-';
  }
}
static void ProcessTypedef(Type type, raw_ostream &os, unsigned childLv) {
  DwarfTagTypedef tpdef{type};
  // PrintDW_AT_type(type, type.find(DW_AT_type).value(), os);
  // 一直找，直到找到不是typedef
  os << formatv("typedef: {0}", tpdef.Name());
  for (type = tpdef.Type(); type.getTag() == DW_TAG_typedef;
       type = tpdef.Type()) {
    tpdef = type;
    os << formatv(" -> {0}", tpdef.Name());
  }
  os << '\n';
  ProcessType(type, os, childLv + 1);
}
static void ProcessArrayType(Type type, raw_ostream &os, unsigned childLv) {
  (void)childLv;
  // PrintDW_AT_type(type, type.find(DW_AT_type).value(), os);
  auto array = DwarfTagArrayType(type);
  os << formatv("array of {0} length {1}\n", GetDW_AT_name(array.ElementType()),
                array.Length());
}
static void ProcessBaseType(Type type, raw_ostream &os, unsigned childLv) {
  (void)childLv;
  DwarfTagBaseType bt{type};
  os << formatv("base type {0} size {1}\n", bt.Name(), bt.ByteSize());
}
static void ProcessType(Type type, raw_ostream &os, unsigned childLv) {
  // recursive
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
    ProcessBaseType(type, os, childLv);
    break;
  case DW_TAG_array_type:
    ProcessArrayType(type, os, childLv);
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
  auto type = FindVariableType(variable, VariableName, OS);
  if (!IsStructType(type)) {
    OS << "variable type is not a struct\n";
    exit(EXIT_FAILURE);
  }
  ProcessType(type, OS, 0);
  return false;
}
int main(int argc, char **argv) {
  if (argc != 3) {
    llvm::outs() << "usage: StructDump <elf> <variable>\n";
    exit(EXIT_FAILURE);
  }
  llvm::InitLLVM X(argc, argv); // catch SIGABRT to print stacktrace
  errs().tie(&outs());
  StringRef FileName = argv[1];
  StringRef VariableName = argv[2];
  handleFile(FileName, dumpObjectFile, VariableName, llvm::outs());
}
