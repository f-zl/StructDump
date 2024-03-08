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
struct DwarfTagArrayType : llvm::DWARFDie { // DW_TAG_array_type
  DwarfTagArrayType(DWARFDie die) : DWARFDie{die} {
    assert(die.getTag() == llvm::dwarf::DW_TAG_array_type);
  }
  // DWARF5标准说array_type一定有DW_AT_name，但dump出来好像没有
  DWARFDie ElementType() const { // array一定有type
    auto type = find(llvm::dwarf::DW_AT_type);
    auto unit = getDwarfUnit();
    auto offset = type->getRawUValue() + unit->getOffset();
    return unit->getDIEForOffset(offset);
  }
  uint64_t Length() const {
    for (auto child = getFirstChild(); child; child = child.getSibling()) {
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
struct DwarfTagMember : llvm::DWARFDie { // DW_TAG_member
  DwarfTagMember(DWARFDie die) : DWARFDie{die} {
    assert(die.getTag() == llvm::dwarf::DW_TAG_member);
  }
  const char *Name() const { // member一定有name，除非是匿名union
    return GetDW_AT_name(*this);
  }
  DWARFDie Type() const { // member一定有type
    auto offset = this->find(llvm::dwarf::DW_AT_type)->getRawUValue();
    auto unit = getDwarfUnit();
    return unit->getDIEForOffset(offset + unit->getOffset());
  }
  uint64_t MemberOffset() const { // output of offsetof()
                                  // TODO 可能没有data_member_location
    return find(llvm::dwarf::DW_AT_data_member_location)
        ->getAsUnsignedConstant()
        .value();
  }
};
struct DwarfTagStructureType : llvm::DWARFDie {
  // use `for(auto child = getChild(); child; child = child.getSibling())` to
  // iterate members
  // TODO tag name
  DwarfTagStructureType(DWARFDie die) : DWARFDie{die} {
    assert(die.getTag() == llvm::dwarf::DW_TAG_structure_type);
  }
  uint64_t ByteSize() const { return GetDW_AT_byte_size(*this); }
};