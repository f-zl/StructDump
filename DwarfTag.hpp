#pragma once
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
  // from DWARFFormValue::dump
  auto offset = die.find(llvm::dwarf::DW_AT_type)->getRawUValue();
  auto unit = die.getDwarfUnit();
  auto unitOffset = unit->getOffset();
  return unit->getDIEForOffset(offset + unitOffset);
}
struct DwarfTagArrayType { // DW_TAG_array_type
  llvm::DWARFDie die;
  DwarfTagArrayType(llvm::DWARFDie die) : die{die} {
    assert(die.getTag() == llvm::dwarf::DW_TAG_array_type);
  }
  // DWARF5 standard says array_type must have DW_AT_name. but dump doesn't show
  // DW_AT_name
  llvm::DWARFDie ElementType() const { // array must have type
    return GetDW_AT_type(die);
  }
  uint64_t Length() const {
    for (auto child = die.getFirstChild(); child; child = child.getSibling()) {
      switch (child.getTag()) { // the length is in subrange or enumeration
      case llvm::dwarf::DW_TAG_subrange_type: { // gcc uses this
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
  const char *Name() const { // member must have name, unless anonymous union
    return GetDW_AT_name(die);
  }
  llvm::DWARFDie Type() const { // member must have type
    return GetDW_AT_type(die);
  }
  uint64_t MemberOffset() const { // output of offsetof()
                                  // TODO there may be no data_member_location
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
  const char *Name() const { return GetDW_AT_name(die); }
  uint64_t ByteSize() const { return GetDW_AT_byte_size(die); }
  // DW_AT_encoding: DW_ATE_float, DW_ATE_signed, DW_ATE_unsigned, etc
};