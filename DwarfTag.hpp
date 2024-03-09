#include <llvm/BinaryFormat/Dwarf.h>
#include <llvm/DebugInfo/DWARF/DWARFDie.h>
#include <llvm/DebugInfo/DWARF/DWARFUnit.h>

// use uint64_t for size_t to analyze 64-bit elf on 32-bit machine
inline uint64_t GetDW_AT_byte_size(llvm::DWARFDie die) {
  return die.find(llvm::dwarf::DW_AT_byte_size)
      .value()
      .getAsUnsignedConstant()
      .value();
}
inline const char *GetDW_AT_name(llvm::DWARFDie die) {
  return die.find(llvm::dwarf::DW_AT_name)->getAsCString().get();
}
inline llvm::DWARFDie GetDW_AT_type(llvm::DWARFDie die) {
  // DWARFFormValue::getForm()=DW_FORM_ref4
  // 参照DWARFFormValue::dump
  auto offset = die.find(llvm::dwarf::DW_AT_type)->getRawUValue();
  auto unit = die.getDwarfUnit();
  return unit->getDIEForOffset(offset + unit->getOffset());
}
struct DwarfTagArrayType { // DW_TAG_array_type
  llvm::DWARFDie die;
  DwarfTagArrayType(llvm::DWARFDie die) : die{die} {
    assert(die.getTag() == llvm::dwarf::DW_TAG_array_type);
  }
  // DWARF5标准说array_type一定有DW_AT_name，但dump出来好像没有
  llvm::DWARFDie ElementType() const { // array一定有type
    return GetDW_AT_type(die);
  }
  uint64_t Length() const {
    for (auto child = die.getFirstChild(); child; child = child.getSibling()) {
      switch (child.getTag()) { // 长度在subrange或enumeration
      case llvm::dwarf::DW_TAG_subrange_type: { // gcc用的是这个
        auto value = child.find(llvm::dwarf::DW_AT_upper_bound).value();
        return value.getAsUnsignedConstant().value() + 1;
      } break;
      case llvm::dwarf::DW_TAG_enumeration_type:
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
  llvm::DWARFDie die;
  DwarfTagMember(llvm::DWARFDie die) : die{die} {
    assert(die.getTag() == llvm::dwarf::DW_TAG_member);
  }
  const char *Name() const { // member一定有name，除非是匿名union
    return GetDW_AT_name(die);
  }
  llvm::DWARFDie Type() const { // member一定有type
    return GetDW_AT_type(die);
  }
  uint64_t MemberOffset() const { // output of offsetof()
                                  // TODO 可能没有data_member_location
    return die.find(llvm::dwarf::DW_AT_data_member_location)
        ->getAsUnsignedConstant()
        .value();
  }
};
struct DwarfTagStructureType {
  llvm::DWARFDie die;
  // use `for(auto child = getChild(); child; child = child.getSibling())` to
  // iterate members
  DwarfTagStructureType(llvm::DWARFDie die) : die{die} {
    assert(die.getTag() == llvm::dwarf::DW_TAG_structure_type);
  }
  uint64_t ByteSize() const { return GetDW_AT_byte_size(die); }
  const char *TagName() const {
    auto value = die.find(llvm::dwarf::DW_AT_name);
    if (value) {
      return value->getAsCString().get();
    }
    return "(anonymous)";
  }
};
struct DwarfTagTypedef {
  llvm::DWARFDie die;
  DwarfTagTypedef(llvm::DWARFDie die) : die{die} {
    assert(die.getTag() == llvm::dwarf::DW_TAG_typedef);
  }
  llvm::DWARFDie Type() { return GetDW_AT_type(die); }
  const char *Name() const { return GetDW_AT_name(die); }
};
struct DwarfTagBaseType {
  llvm::DWARFDie die;
  DwarfTagBaseType(llvm::DWARFDie die) : die{die} {
    assert(die.getTag() == llvm::dwarf::DW_TAG_base_type);
  }
  const char *Name() const {
    return die.find(llvm::dwarf::DW_AT_name)->getAsCString().get();
  }
  uint64_t ByteSize() const {
    return die.find(llvm::dwarf::DW_AT_byte_size)
        ->getAsUnsignedConstant()
        .value();
  }
  // DW_AT_encoding: DW_ATE_float, DW_ATE_signed, DW_ATE_unsigned等
};