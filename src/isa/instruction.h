#pragma once

#include <climits>
#include <experimental/type_traits>
#include <memory>
#include <string_view>
#include <vector>

#include <QList>

#include "../binutils.h"
#include "isa_defines.h"
#include <STLExtras.h>

namespace Ripes {

/** @brief Ensure that the template parameters for BitRangesImpl are always the
 * BitRange type.
 * Used in static assertions.
 * @param BitRange...: Types that might be BitRange
 */
template <typename... MaybeBitRange>
using IsBitRanges = decltype((
    ((MaybeBitRange::N() + MaybeBitRange::Start() + MaybeBitRange::Stop() +
      MaybeBitRange::Width()),
     MaybeBitRange::Apply(MaybeBitRange::Mask()),
     MaybeBitRange::Decode(MaybeBitRange::Mask()),
     std::declval<MaybeBitRange>().n() + std::declval<MaybeBitRange>().start() +
         std::declval<MaybeBitRange>().stop() +
         std::declval<MaybeBitRange>().width(),
     std::declval<MaybeBitRange>().apply(MaybeBitRange::Mask()),
     std::declval<MaybeBitRange>().decode(MaybeBitRange::Mask())),
    ...));

/** No-template, abstract class that describes a BitRange. */
struct BitRangeBase {
  virtual unsigned n() const = 0;
  virtual unsigned start() const = 0;
  virtual unsigned stop() const = 0;
  virtual unsigned width() const = 0;
  virtual Instr_T getMask() const = 0;
  virtual Instr_T apply(Instr_T value) const = 0;
  virtual Instr_T decode(Instr_T instruction) const = 0;

  bool operator==(const BitRangeBase &other) const {
    return start() == other.start() && stop() == other.stop();
  }
  bool operator<(const BitRangeBase &other) const {
    return (start() == other.start()) ? stop() < other.stop()
                                      : start() < other.start();
  }
};

// TODO: Remove N if unnecessary
/** @brief A range of bits determined at compile-time
 * NOTE: start/stop values for bitranges are inclusive..
 * @param _start: Starting index of the range
 * @param _stop: Ending index of the range (inclusive)
 */
template <unsigned _start, unsigned _stop, unsigned _N = 32>
struct BitRange : public BitRangeBase {
  static_assert(isPowerOf2(_N), "Bitrange N must be power of 2");
  static_assert(_start <= _stop && _stop < _N, "invalid range");

  constexpr static unsigned N() { return _N; }
  constexpr static unsigned Start() { return _start; }
  constexpr static unsigned Stop() { return _stop; }
  constexpr static unsigned Width() { return _stop - _start + 1; }
  constexpr static Instr_T Mask() { return vsrtl::generateBitmask(Width()); }
  constexpr static Instr_T Apply(Instr_T value) {
    return (value & Mask()) << _start;
  }
  constexpr static Instr_T Decode(Instr_T instruction) {
    return (instruction >> _start) & Mask();
  }

  unsigned n() const override { return _N; }
  unsigned start() const override { return _start; }
  unsigned stop() const override { return _stop; }
  unsigned width() const override { return Width(); }
  Instr_T getMask() const override { return Mask(); }
  Instr_T apply(Instr_T value) const override { return Apply(value); }
  Instr_T decode(Instr_T instruction) const override {
    return Decode(instruction);
  }
};

/** A set of BitRanges.
 * @param BitRanges: A set of BitRange types. All BitRanges must not overlap.
 */
template <typename... BitRanges>
struct BitRangesImpl {
  /// Combine this type with a set of more BitRanges
  template <typename... OtherBitRanges>
  using CombinedBitRanges = BitRangesImpl<BitRanges..., OtherBitRanges...>;

  /// Combine this type with another BitRangesImpl
  template <typename OtherBitRangeImpl>
  using CombineWith =
      typename OtherBitRangeImpl::template CombinedBitRanges<BitRanges...>;

  /// Returns the combined width of all BitRanges
  constexpr static unsigned Width() { return (BitRanges::Width() + ... + 0); }

  /// Adds all BitRanges to a vector.
  /// This is useful for querying BitRanges at runtime
  static void
  RetrieveBitRanges(std::vector<std::shared_ptr<BitRangeBase>> &bitRanges) {
    (bitRanges.push_back(std::make_shared<BitRanges>()), ...);
  }

private:
  /// Compile-time verification using recursive templates and static_assert
  template <typename...>
  struct Verify {
    enum { nonOverlapping = true, equalWidth = true };
  };
  template <typename FirstRange, typename SecondRange, typename... OtherRanges>
  struct Verify<FirstRange, SecondRange, OtherRanges...> {
    /// Returns true if FirstRange and SecondRange are not overlapping
    constexpr static bool IsNotOverlapping() {
      return (FirstRange::Start() > SecondRange::Stop() ||
              FirstRange::Stop() < SecondRange::Start());
    }
    /// Returns true if FirstRange and SecondRange have an equal width
    constexpr static bool HasEqualWidth() {
      return (FirstRange::N() == SecondRange::N());
    }
    enum {
      // Set to true if all BitRanges have been verified
      nonOverlapping = (IsNotOverlapping() &&
                        Verify<FirstRange, OtherRanges...>::nonOverlapping &&
                        Verify<SecondRange, OtherRanges...>::nonOverlapping),
      equalWidth =
          (HasEqualWidth() && Verify<FirstRange, OtherRanges...>::equalWidth &&
           Verify<SecondRange, OtherRanges...>::equalWidth)
    };
  };
  template <typename FirstRange>
  struct Verify<FirstRange> {
    enum { nonOverlapping = true, equalWidth = true };
  };

  static_assert(Verify<BitRanges...>::nonOverlapping,
                "BitRanges overlap with each other");
  // TODO(raccog): I'm not sure what BitRange::N() is used for yet, so I'm
  // disabling this assertion for now
  //  static_assert(Verify<BitRanges...>::equalWidth,
  //                "BitRanges do not have an equal width");
  static_assert(std::experimental::is_detected_v<IsBitRanges, BitRanges...>,
                "BitRangesImpl can only contain BitRange types");

public:
  /// Used in instruction class static assertion.
  constexpr static bool IsVerified = (Verify<BitRanges...>::nonOverlapping &&
                                      Verify<BitRanges...>::equalWidth);
};

struct OpPartBase;

// TODO: Could this be the base class in place of virtual functions?
/** @brief No-template, non-abstract class that describes an OpPart.
 * This is useful for the assembly matcher so that OpParts can be used as a
 * key in a std::map.
 */
struct OpPartMatcher {
  OpPartMatcher(unsigned _value, unsigned _start, unsigned _stop, unsigned _N,
                const OpPartBase *_opPart)
      : value(_value), start(_start), stop(_stop), N(_N), opPart(_opPart) {}

  unsigned value;
  unsigned start, stop, N;
  /// Contains a pointer to the original OpPart
  const OpPartBase *opPart;

  bool operator==(const OpPartMatcher &other) const {
    return value == other.value && start == other.start && stop == other.stop;
  }
  bool operator<(const OpPartMatcher &other) const {
    if (start == other.start && stop == other.stop)
      return value < other.value;
    return start < other.start;
  }
};

/** @brief No-template, abstract class that describes an OpPart. */
struct OpPartBase {
  virtual unsigned value() const = 0;
  virtual const BitRangeBase &range() const = 0;
  /// Returns a struct that can be used in the assembly matcher
  OpPartMatcher getMatcher() const {
    return OpPartMatcher(value(), range().start(), range().stop(), range().n(),
                         this);
  }

  /// Returns true if this OpPart is contained in the instruction.
  bool matches(Instr_T instruction) const {
    return range().decode(instruction) == value();
  }

  bool operator<(const OpPartBase &other) const {
    if (range() == other.range())
      return value() < other.value();
    return range() < other.range();
  }
};

/** @brief A segment of an operation-identifying field of an instruction.
 * @param _value: The value that identifies this OpPart.
 * @param _BitRange: The range of bits that contain this OpPart. Must be a
 * BitRange type.
 */
template <unsigned _value, typename _BitRange>
class OpPart : public OpPartBase {
public:
  using BitRange = _BitRange;

  unsigned value() const override { return _value; }
  const BitRangeBase &range() const override { return *m_range.get(); }

  constexpr static unsigned Value() { return _value; }

  /// Applies this OpPart's encoding to the instruction.
  constexpr static void Apply(Instr_T &instruction) {
    instruction |= BitRange::Apply(Value());
  }
  /// Returns true if this OpPart is contained in the instruction.
  constexpr static bool Matches(Instr_T instruction) {
    return BitRange::Decode(instruction) == _value;
  }

private:
  std::unique_ptr<BitRange> m_range = std::make_unique<BitRange>();

  // Ensure value is not too large to fit in BitRange
  static_assert(isUInt<BitRange::Stop() - BitRange::Start() + 1>(_value),
                "OpPart value is too large to fit in BitRange.");
  static_assert(std::experimental::is_detected_v<IsBitRanges, BitRange>,
                "OpPart can only contain a BitRange type");
};

/** @brief An OpPart for declaring BitRanges of unused zeros */
template <unsigned start, unsigned stop>
struct OpPartZeroes : public OpPart<0, BitRange<start, stop>> {};

/** @brief Function type for resolving symbols. */
using ResolveSymbolFunc =
    std::function<Result<>(const Location &, Reg_T, Instr_T &, Reg_T)>;

/** @brief A request to link a field of an instruction to a symbol. */
struct FieldLinkRequest {
  ResolveSymbolFunc resolveSymbol;
  QString symbol = QString();
  QString relocation = QString();
};

// TODO: Construct in function instead of being declared as a static array
template <unsigned numParts, typename... OpParts>
static std::array<std::unique_ptr<OpPartBase>, numParts> OP_PARTS = {
    (std::make_unique<OpParts>())...};

// TODO: Assert that OpParts are of OpPart type
/** @brief A set of OpParts that identifies an instruction.
 * @param OpParts: A set of OpPart types. All OpParts' BitRanges must not
 * overlap.
 */
template <typename... OpParts>
class OpcodeImpl {
public:
  using BitRanges = BitRangesImpl<typename OpParts::BitRange...>;

  /// Applies each OpPart's encoding to the instruction.
  constexpr static void Apply(Instr_T &instruction, FieldLinkRequest &) {
    (OpParts::Apply(instruction), ...);
  }
  /// Returns the number of OpParts in this opcode.
  constexpr static unsigned NumParts() { return sizeof...(OpParts); }
  /// Returns a pointer to a dynamically accessible OpPart. (needed for the
  /// assembly matcher)
  constexpr static const OpPartBase *GetOpPart(unsigned partIndex) {
    assert(partIndex < NumParts());

    return OP_PARTS<NumParts(), OpParts...>[partIndex].get();
  }
  /// Adds all BitRanges to a vector.
  /// This is useful for querying BitRanges at runtime
  constexpr static void
  RetrieveBitRanges(std::vector<std::shared_ptr<BitRangeBase>> &bitRanges) {
    BitRanges::RetrieveBitRanges(bitRanges);
  }
};

/**
 * @brief The InstrRes struct is returned by any assembler. Contains the
 * assembled instruction alongside a flag noting whether the instruction needs
 * additional linkage (ie. for symbol resolution).
 */
struct InstrRes {
  Instr_T instruction = 0;
  FieldLinkRequest linksWithSymbol;
};

using AssembleRes = Result<InstrRes>;

// TODO: Figure out how to assign token indices automatically
/** @brief An instruction field defined at compile-time.
 * @param _tokenIndex: The index of this field in an assembly instruction
 * (starting at 0).
 * @param _BitRanges: A set of BitRanges that define which bits contain field in
 * the instruction. Must be BitRangesImpl type.
 */
template <unsigned _tokenIndex, typename _BitRanges>
struct Field {
  using BitRanges = _BitRanges;
  constexpr static unsigned TokenIndex() { return _tokenIndex; }
};

/** @brief A set of fields for an instruction
 * @param Fields: The set of Field types. Field BitRanges must not overlap.
 * Fields must have sequential indices, starting at 0.
 */
template <typename... Fields>
class FieldsImpl {
private:
  /// Structs for combining BitRanges from each field
  template <typename... OtherFields>
  struct FieldRanges {
    using BitRanges = BitRangesImpl<>;
  };
  template <typename FirstField, typename SecondField, typename... OtherFields>
  struct FieldRanges<FirstField, SecondField, OtherFields...> {
    // Combined BitRanges from all fields
    using BitRanges = typename FirstField::BitRanges::template CombineWith<
        typename SecondField::BitRanges>::
        template CombineWith<typename FieldRanges<OtherFields...>::BitRanges>;
  };
  template <typename FirstField, typename SecondField>
  struct FieldRanges<FirstField, SecondField> {
    // Combined BitRanges from two fields
    using BitRanges = typename FirstField::BitRanges::template CombineWith<
        typename SecondField::BitRanges>;
  };
  template <typename FirstField>
  struct FieldRanges<FirstField> {
    using BitRanges = typename FirstField::BitRanges;
  };

public:
  /// Combined BitRanges from each field in the set
  using BitRanges = typename FieldRanges<Fields...>::BitRanges;

  /// Applies each Field's encoding to the instruction.
  static Result<> Apply(const TokenizedSrcLine &tokens, Instr_T &instruction,
                        FieldLinkRequest &linksWithSymbol) {
    Result<> res = std::monostate();
    (
        [&] {
          if (auto err = Fields::Apply(tokens, instruction, linksWithSymbol);
              err.isError() && !res.isError()) {
            res = std::get<Error>(err);
          }
        }(),
        ...);
    return res;
  }
  /// Decodes each field into an assembly instruction line.
  constexpr static bool Decode(const Instr_T instruction, const Reg_T address,
                               const ReverseSymbolMap &symbolMap,
                               LineTokens &line);

  /// Returns the number of Fields in this set.
  constexpr static unsigned NumFields() { return sizeof...(Fields); }
  /// Adds all BitRanges to a vector.
  /// This is useful for querying BitRanges at runtime
  constexpr static void
  RetrieveBitRanges(std::vector<std::shared_ptr<BitRangeBase>> &bitRanges) {
    BitRanges::RetrieveBitRanges(bitRanges);
  }

private:
  // TODO: Verify that:
  // * Registers are not duplicated?? (might be difficult to verify)
  /// Verify that each field has sequential indices.
  template <typename...>
  struct Verify {
    enum { hasSequentialIndices = true };
  };
  template <typename FirstField, typename SecondField, typename... OtherFields>
  struct Verify<FirstField, SecondField, OtherFields...> {
    /// Returns true if SecondField has an index that is directly after
    /// FirstField's index
    constexpr static bool IsInOrder() {
      return (FirstField::TokenIndex() + 1 == SecondField::TokenIndex());
    }
    enum {
      hasSequentialIndices =
          (IsInOrder() &&
           Verify<SecondField, OtherFields...>::hasSequentialIndices),
    };
  };
  template <typename FirstField>
  struct Verify<FirstField> {
    enum { hasSequentialIndices = true };
  };
  /// Verify that the first field has an index of 0.
  template <typename...>
  struct VerifyFirstIndex {
    enum { indexStartsAtZero = true };
  };
  template <typename FirstField, typename... OtherFields>
  struct VerifyFirstIndex<FirstField, OtherFields...> {
    enum { indexStartsAtZero = (FirstField::TokenIndex() == 0) };
  };

  static_assert(VerifyFirstIndex<Fields...>::indexStartsAtZero,
                "First field' index is not 0");
  static_assert(Verify<Fields...>::hasSequentialIndices,
                "Fields have duplicate indices");
};

template <typename... Fields>
constexpr bool FieldsImpl<Fields...>::Decode(const Instr_T instruction,
                                             const Reg_T address,
                                             const ReverseSymbolMap &symbolMap,
                                             LineTokens &line) {
  bool failure = false;
  ((failure |= !Fields::Decode(instruction, address, symbolMap, line)), ...);
  return !failure;
}
template <>
constexpr bool FieldsImpl<>::Decode(const Instr_T, const Reg_T,
                                    const ReverseSymbolMap &, LineTokens &) {
  return true;
}

/**
 * @brief Reg
 * @param RegImpl: A type that declares `constexpr static std::string_view
 * Name`. This defines the name of this register.
 * @param tokenIndex: Index within a list of decoded instruction tokens that
 * corresponds to the register index
 * @param BitRange: range in instruction field containing register index value
 * @param RegInfo: A type that declares 2 functions:
 * `static unsigned int RegNumber(const QString &reg, bool &success)` and
 * `static QString RegName(unsigned i)`
 */
template <typename RegImpl, unsigned tokenIndex, typename BitRange,
          typename RegInfo>
struct Reg : public Field<tokenIndex, BitRangesImpl<BitRange>> {
  Reg() : regsd(RegImpl::Name.data()) {}

  /// Applies this register's encoding to the instruction.
  static Result<> Apply(const TokenizedSrcLine &line, Instr_T &instruction,
                        FieldLinkRequest &) {
    if (tokenIndex + 1 >= line.tokens.size()) {
      return Error(line, "Required field '" + QString(RegImpl::Name.data()) +
                             "' (index " + QString::number(tokenIndex) +
                             ") not provided");
    }
    const auto &regToken = line.tokens.at(tokenIndex + 1);
    bool success = false;
    unsigned regIndex = RegInfo::RegNumber(regToken, success);
    if (!success) {
      return Error(line, "Unknown register '" + regToken + "'");
    }
    instruction |= BitRange::Apply(regIndex);
    return std::monostate();
  }
  /// Decodes this register into its name. Adds it to the assembly line.
  static bool Decode(const Instr_T instruction, const Reg_T,
                     const ReverseSymbolMap &, LineTokens &line) {
    const unsigned regNumber = BitRange::Decode(instruction);
    const Token registerName(RegInfo::RegName(regNumber));
    if (registerName.isEmpty()) {
      return false;
    }
    line.push_back(registerName);
    return true;
  }

  const QString regsd = "reg";
};

/** @brief A part of an immediate field.
 * @param _offset: The offset applied to this part when it is constructed into
 * an immediate value.
 * @param _BitRange: The range of bits that contain this part.
 */
template <unsigned _offset, typename _BitRange>
struct ImmPartBase {
  using BitRange = _BitRange;
  // Declaration of BitRanges allows ImmPart to be compatible with
  // ImmPartsImpl
  using BitRanges = BitRangesImpl<BitRange>;

  /// Returns the offset applied to this part when it is constructed into an
  /// immediate value.
  constexpr static unsigned Offset() { return _offset; }

  /// Applies this immediate part's encoding to the instruction.
  constexpr static void Apply(const Instr_T value, Instr_T &instruction) {
    instruction |= BitRange::Apply(value >> _offset);
  }
  /// Decodes this immediate part into its value, combining it with other
  /// values.
  constexpr static void Decode(Instr_T &value, const Instr_T instruction) {
    value |= BitRange::Decode(instruction) << _offset;
  }

private:
  static_assert(BitRange::Width() + _offset < BitRange::N(),
                "ImmPart does not fit in BitRange size. Check ImmPart offset"
                " and BitRange width");
};

template <unsigned offset, unsigned start, unsigned stop, unsigned N = 32>
using ImmPart = ImmPartBase<offset, BitRange<start, stop, N>>;

/** @brief A set of immediate parts for an immediate field.
 * @param ImmParts: The set of ImmPart types. ImmPart BitRanges must not
 * overlap. ImmParts must not overlap when constructed into an immediate value
 * with their offsets applied.
 */
template <typename... ImmParts>
struct ImmPartsImpl {
  using BitRanges = BitRangesImpl<typename ImmParts::BitRange...>;

  /// Applies each immediate part's encoding to the instruction.
  constexpr static void Apply(const Instr_T value, Instr_T &instruction) {
    (ImmParts::Apply(value, instruction), ...);
  }
  /// Decodes this immediate into its value by combining values from each part.
  constexpr static void Decode(Instr_T &value, const Instr_T instruction) {
    (ImmParts::Decode(value, instruction), ...);
  }

private:
  // TODO: This is probably not necessary because all ranges are checked for
  // overlapping at the end
  /// Verify that each immediate does not overlap.
  template <typename FirstPart, typename... OtherParts>
  struct Verify {};
  template <typename FirstPart, typename SecondPart, typename... OtherParts>
  struct Verify<FirstPart, SecondPart, OtherParts...> {
    /// Returns true if FirstPart and SecondPart are not overlapping
    constexpr static bool IsNotOverlapping() {
      return (FirstPart::Offset() >=
                  (SecondPart::Offset() + SecondPart::BitRange::Width()) ||
              SecondPart::Offset() >=
                  (FirstPart::Offset() + FirstPart::BitRange::Width()));
    }
    enum {
      nonOverlapping = (IsNotOverlapping() &&
                        Verify<FirstPart, OtherParts...>::nonOverlapping &&
                        Verify<SecondPart, OtherParts...>::nonOverlapping)
    };
  };
  template <typename FirstPart>
  struct Verify<FirstPart> {
    enum { nonOverlapping = true };
  };

  static_assert(Verify<ImmParts...>::nonOverlapping,
                "Combined ImmParts overlap with each other");
};

enum class Repr { Unsigned, Signed, Hex };
enum class SymbolType { None, Relative, Absolute };

static inline Radix reprToRadix(Repr repr) {
  if (repr == Repr::Unsigned)
    return Radix::Unsigned;
  if (repr == Repr::Signed)
    return Radix::Signed;
  if (repr == Repr::Hex)
    return Radix::Hex;
  return Radix::Unsigned;
}

typedef Reg_T (*SymbolTransformer)(Reg_T);

/// The default immediate transformer. Returns the value unchanged.
inline Reg_T defaultTransformer(Reg_T reg) { return reg; }

/**
 * @brief Imm
 * @param tokenIndex: Index within a list of decoded instruction tokens that
 * corresponds to the immediate
 * @param width: bit-width of the immediate
 * @param repr: Representation of the immediate
 * @param symbolType: Set if this immediate refers to a relative or absolute
 * symbol.
 * @param transformer: Optional function used to process the immediate
 * provided by a symbol value, before the immediate value is applied.
 * @param ImmParts: A set of ImmPart types that define the encoding of this
 * immediate field.
 */
template <unsigned tokenIndex, unsigned width, Repr repr, typename ImmParts,
          SymbolType symbolType, SymbolTransformer transformer>
struct ImmBase : public Field<tokenIndex, typename ImmParts::BitRanges> {
  static_assert(width >= ImmParts::BitRanges::Width(),
                "An immediate's combined parts are larger than its width");

  using Reg_T_S = typename std::make_signed<Reg_T>::type;
  using Reg_T_U = typename std::make_unsigned<Reg_T>::type;

  /// Converts a string to its immediate value (if it exists). Success is set to
  /// false if this fails.
  constexpr static int64_t GetImm(const QString &immToken, bool &success,
                                  ImmConvInfo &convInfo) {
    return repr == Repr::Signed
               ? getImmediateSext32(immToken, success, &convInfo)
               : getImmediate(immToken, success, &convInfo);
  }

  /// Returns an error if `value` does not fit in this immediate.
  static Result<> CheckFitsInWidth(Reg_T_S value, const Location &sourceLine,
                                   ImmConvInfo &convInfo,
                                   QString token = QString()) {
    bool err = false;
    if (repr != Repr::Signed) {
      if (!isUInt(width, value)) {
        err = true;
        if (token.isEmpty())
          token = QString::number(static_cast<Reg_T_U>(value));
      }
    } else {

      // In case of a bitwize (binary or hex) radix, interpret the value as
      // legal if it fits in the width of this immediate (equal to an unsigned
      // immediate check). e.g. a signed immediate value of 12 bits must be
      // able to accept 0xAFF.
      bool isBitwize =
          convInfo.radix == Radix::Hex || convInfo.radix == Radix::Binary;
      if (isBitwize) {
        err = !isUInt(width, value);
      }

      if (!isBitwize || (isBitwize && err)) {
        // A signed representation using an integer value in assembly OR a
        // negative bitwize value which is represented in its full length
        // (e.g. 0xFFFFFFF1).
        err = !isInt(width, value);
      }

      if (err)
        if (token.isEmpty())
          token = QString::number(static_cast<Reg_T_S>(value));
    }

    if (err) {
      return Error(sourceLine, "Immediate value '" + token +
                                   "' does not fit in " +
                                   QString::number(width) + " bits");
    }

    return Result<>::def();
  }

  /// Symbol resolver function for this immediate.
  static Result<> ApplySymbolResolution(const Location &loc, Reg_T symbolValue,
                                        Instr_T &instruction, Reg_T address) {
    ImmConvInfo convInfo;
    convInfo.radix = reprToRadix(repr);
    Reg_T adjustedValue = symbolValue;
    if (symbolType == SymbolType::Relative)
      adjustedValue -= address;

    adjustedValue = transformer(adjustedValue);

    if (auto res = CheckFitsInWidth(adjustedValue, loc, convInfo);
        res.isError())
      return res.error();

    ImmParts::Apply(adjustedValue, instruction);

    return Result<>::def();
  }

  /// Applies this immediate's encoding to the instruction.
  static Result<> Apply(const TokenizedSrcLine &line, Instr_T &instruction,
                        FieldLinkRequest &linksWithSymbol) {
    if (tokenIndex + 1 >= line.tokens.size()) {
      return Error(line, "Required immediate with field index '" +
                             QString::number(tokenIndex) + "' not provided");
    }
    bool success = false;
    const Token &immToken = line.tokens[tokenIndex + 1];
    ImmConvInfo convInfo;
    Reg_T_S value = GetImm(immToken, success, convInfo);

    if (!success) {
      // Could not directly resolve immediate. Register it as a symbol to link
      // to.
      linksWithSymbol.resolveSymbol = ApplySymbolResolution;
      linksWithSymbol.symbol = immToken;
      linksWithSymbol.relocation = immToken.relocation();
      return Error(line, "Could not resolve immediate");
    }

    if (auto res = CheckFitsInWidth(value, line, convInfo, immToken);
        res.isError())
      return res.error();

    ImmParts::Apply(value, instruction);
    return std::monostate();
  }
  /// Decodes this immediate part into its value, adding it to the assembly
  /// line.
  constexpr static bool Decode(const Instr_T instruction, const Reg_T address,
                               const ReverseSymbolMap &symbolMap,
                               LineTokens &line) {
    Instr_T reconstructed = 0;
    ImmParts::Decode(reconstructed, instruction);
    if (repr == Repr::Signed) {
      line.push_back(QString::number(vsrtl::signextend(reconstructed, width)));
    } else if (repr == Repr::Unsigned) {
      line.push_back(QString::number(reconstructed));
    } else {
      line.push_back("0x" + QString::number(reconstructed, 16));
    }

    if (symbolType != SymbolType::None) {
      const int value = vsrtl::signextend(reconstructed, width);
      const Reg_T symbolAddress =
          value + (symbolType == SymbolType::Absolute ? 0 : address);
      if (symbolMap.count(symbolAddress)) {
        line.push_back("<" + symbolMap.at(symbolAddress).v + ">");
      }
    }

    return true;
  }
};

/** @brief Shorthand for an Immediate with the default value transformer */
template <unsigned tokenIndex, unsigned width, Repr repr, typename ImmParts,
          SymbolType symbolType>
using ImmSym =
    ImmBase<tokenIndex, width, repr, ImmParts, symbolType, defaultTransformer>;

/** @brief Shorthand for an Immediate with the default value transformer and no
 * symbol type. */
template <unsigned tokenIndex, unsigned width, Repr repr, typename ImmParts>
using Imm = ImmBase<tokenIndex, width, repr, ImmParts, SymbolType::None,
                    defaultTransformer>;

/** @brief A no-template, abstract class that defines an instruction. */
class InstructionBase {
public:
  virtual ~InstructionBase() = default;
  /// Assembles a line of tokens into an encoded program.
  virtual AssembleRes assemble(const TokenizedSrcLine &tokens) = 0;
  /// Disassembles an encoded program into a tokenized assembly program.
  virtual Result<LineTokens>
  disassemble(const Instr_T instruction, const Reg_T address,
              const ReverseSymbolMap &symbolMap) const = 0;
  /// Returns a pointer to a dynamically accessible OpPart. (needed for the
  /// assembly matcher)
  virtual const OpPartBase *getOpPart(unsigned partIndex) const = 0;
  /// Returns the name of this instruction.
  virtual const QString &name() const = 0;
  /// Returns the number of OpParts in this instruction.
  virtual unsigned numOpParts() const = 0;

  /**
   * @brief size
   * @return size of assembled instruction, in bytes.
   */
  unsigned size() const { return m_byteSize; }

  void addExtraMatchCond(const std::function<bool(Instr_T)> &f) {
    m_extraMatchConditions.push_back(f);
  }
  bool hasExtraMatchConds() const { return !m_extraMatchConditions.empty(); }
  bool matchesWithExtras(Instr_T instr) const {
    return llvm::all_of(m_extraMatchConditions,
                        [&](const auto &f) { return f(instr); });
  }

protected:
  /// An optional set of disassembler match conditions, if the default
  /// opcode-based matching is insufficient.
  std::vector<std::function<bool(Instr_T)>> m_extraMatchConditions;
  unsigned m_byteSize = -1;
};

/** @brief Asserts that this instruction has no overlapping fields and has all
 * bits utilized.
 * @param InstrImpl: The instruction type to assert.
 */
template <typename InstrImpl>
struct InstrVerify {
  // TODO: Assert instruction is byte aligned. This requires compile-time
  // knowledge of the register width, which currently does not exist.
  // See enableInstructions() for the static assertion that verifies
  // instructions with this struct
  using BitRanges =
      typename InstrImpl::Opcode::Impl::BitRanges::template CombineWith<
          typename InstrImpl::Fields::Impl::BitRanges>;
  static_assert(BitRanges::IsVerified,
                "Could not verify combined bitranges from Opcode and Fields");
  static_assert(BitRanges::Width() == InstrImpl::InstrBits(),
                "Not all bits are utilized in instruction");
  constexpr static bool IsVerified =
      (BitRanges::IsVerified && BitRanges::Width() == InstrImpl::InstrBits());
};

// TODO: Remove Impl from Opcode::Impl and Fields::Impl?
/** @brief An ISA instruction defined at compile-time.
 * @param InstrImpl: The type defining a single instruction. Must define the
 * following:
 *
 * `using Opcode::Impl = OpcodeImpl`
 * `using Fields::Impl = FieldsImpl`
 * `constexpr static std::string_view Name`
 */
template <typename InstrImpl>
class Instruction : public InstructionBase {
public:
  Instruction() : m_name(InstrImpl::Name.data()) { verify(); }

  AssembleRes assemble(const TokenizedSrcLine &tokens) override {
    Instr_T instruction = 0;
    FieldLinkRequest linksWithSymbol;

    InstrImpl::Opcode::Impl::Apply(instruction, linksWithSymbol);
    if (auto fieldRes = InstrImpl::Fields::Impl::Apply(tokens, instruction,
                                                       linksWithSymbol);
        fieldRes.isError()) {
      return std::get<Error>(fieldRes);
    }

    InstrRes res;
    res.linksWithSymbol = linksWithSymbol;
    res.instruction = instruction;
    return res;
  }
  Result<LineTokens>
  disassemble(const Instr_T instruction, const Reg_T address,
              const ReverseSymbolMap &symbolMap) const override {
    LineTokens line;
    line.push_back(name());
    if (!InstrImpl::Fields::Impl::Decode(instruction, address, symbolMap,
                                         line)) {
      return Error(Location(static_cast<int>(address)), "");
    }
    return line;
  }
  const OpPartBase *getOpPart(unsigned partIndex) const override {
    return InstrImpl::Opcode::Impl::GetOpPart(partIndex);
  }
  const QString &name() const override { return m_name; }
  unsigned numOpParts() const override {
    return InstrImpl::Opcode::Impl::NumParts();
  }

  // TODO: Remove this once all verifications are done at compile-time
  /// Verify that the bitranges specified for this operation:
  /// 1. do not overlap
  /// 2. fully defines the instruction (no bits are unaccounted for)
  /// 3. is byte aligned
  /// Using this information, we also set the size of this instruction.
  void verify() {
    std::vector<std::shared_ptr<BitRangeBase>> bitRanges;
    InstrImpl::Opcode::Impl::RetrieveBitRanges(bitRanges);
    InstrImpl::Fields::Impl::RetrieveBitRanges(bitRanges);

    // 1.
    std::set<unsigned> registeredBits;
    for (auto &range : bitRanges) {
      for (unsigned i = range->start(); i <= range->stop(); ++i) {
        assert(registeredBits.count(i) == 0 &&
               "Bit already registerred by some other field");
        registeredBits.insert(i);
      }
    }

    // 2.
    assert(registeredBits.count(0) == 1 && "Expected bit 0 to be in bit-range");
    // rbegin due to set being sorted.
    unsigned nBits = registeredBits.size();
    if ((nBits - 1) != *registeredBits.rbegin()) {
      std::string err = "Bits '";
      for (unsigned i = 0; i < nBits; ++i) {
        if (registeredBits.count(i) == 0) {
          err += std::to_string(i) + ", ";
        }
      }
      std::cerr << err << "\n";
      assert(false);
    }

    // 3.
    assert(nBits % CHAR_BIT == 0 && "Expected instruction to be byte-aligned");
    m_byteSize = nBits / CHAR_BIT;
  }

private:
  const QString m_name;
};

using InstrMap = std::map<QString, std::shared_ptr<InstructionBase>>;

using InstrVec = std::vector<std::shared_ptr<InstructionBase>>;

} // namespace Ripes
