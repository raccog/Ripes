#pragma once

#include <iostream>
#include <memory>
#include <numeric>

#include "instruction.h"

namespace Ripes {
namespace Assembler {

class Matcher {
  class MatchNode {
  private:
    /// If set, this match node will base its matching on the extra conditions
    /// for a function. Else, matches on the OpPart assoacited with this node.
    bool m_matchOnExtraMatchConds = false;

  public:
    template <typename BitRange>
    MatchNode(OpPart<BitRange> _matcher) : matcher(_matcher) {}
    OpPartBase matcher;
    std::vector<MatchNode> children;
    std::shared_ptr<InstructionBase> instruction;
    void matchOnExtraMatchConds() { m_matchOnExtraMatchConds = true; }

    bool matches(const Instr_T &instr) const {
      return m_matchOnExtraMatchConds ? instruction->matchesWithExtras(instr)
                                      : matcher.matches(instr);
    }

    void print(unsigned depth = 0) const {
      if (depth == 0) {
        std::cout << "root";
      } else {
        for (unsigned i = 0; i < depth; ++i) {
          std::cout << "-";
        }

        if (!instruction ||
            (instruction && depth <= instruction->getOpcode().partCount())) {
          QString matchField =
              QString::number(matcher.range->stop()) + "[" +
              QStringLiteral("%1").arg(matcher.value, matcher.range->width(), 2,
                                       QLatin1Char('0')) +
              "]" + QString::number(matcher.range->start());
          std::cout << matchField.toStdString();
        } else {
          // Extra match conditions must apply
          assert(instruction.get()->hasExtraMatchConds());
        }
        std::cout << " -> ";
      }

      if (instruction) {
        std::cout << instruction.get()->name().toStdString();
        if (instruction.get()->hasExtraMatchConds())
          std::cout << "*";
        std::cout << std::endl;
      } else {
        std::cout << std::endl;
        for (const auto &child : children) {
          child.print(depth + 1);
        }
      }
    }
  };

public:
  Matcher(const std::vector<std::shared_ptr<InstructionBase>> &instructions)
      : m_matchRoot(buildMatchTree(instructions, 1)) {}
  void print() const { m_matchRoot.print(); }

  Result<const InstructionBase *>
  matchInstruction(const Instr_T &instruction) const {
    auto match = matchInstructionRec(instruction, m_matchRoot, true);
    if (match == nullptr) {
      return Error(0, "Unknown instruction");
    }
    return match;
  }

private:
  const InstructionBase *matchInstructionRec(const Instr_T &instruction,
                                             const MatchNode &node,
                                             bool isRoot) const {
    if (isRoot || node.matches(instruction)) {
      if (node.children.size() > 0) {
        for (const auto &child : node.children) {
          if (auto matchedInstr =
                  matchInstructionRec(instruction, child, false);
              matchedInstr != nullptr) {
            return matchedInstr;
          }
        }
      } else if (node.instruction->matchesWithExtras(instruction)) {
        return &(*node.instruction);
      }
    }
    return nullptr;
  }

  MatchNode buildMatchTree(const InstrVec &instructions,
                           const unsigned fieldDepth = 1,
                           const OpPart<BitRange<0, 0, 2>> &matcher =
                               OpPart(0, BitRange<0, 0, 2>())) {
    std::map<OpPartBase, InstrVec> instrsWithEqualOpPart;
    for (const auto &instr : instructions) {
      if (auto instrRef = instr.get()) {
        const size_t nOpParts = instrRef->getOpcode().partCount();
        if (nOpParts < fieldDepth && !instrRef->hasExtraMatchConds()) {
          QString err = "Instruction '" + instr->name() +
                        "' cannot be decoded; aliases with other instruction "
                        "(Needs more discernable parts)\n";
          throw std::runtime_error(err.toStdString().c_str());
        }
        auto &opParts = instrRef->getOpcode().getOpParts();
        const OpPartBase *opPart = nullptr;
        if (fieldDepth > nOpParts)
          opPart = &opParts.back();
        else
          opPart = &opParts[fieldDepth - 1];
        if (nOpParts == fieldDepth &&
            instrsWithEqualOpPart.count(*opPart) != 0) {
          QString err;
          err += "Instruction cannot be decoded; aliases with other "
                 "instruction (Identical to other "
                 "instruction)\n";
          err += instr->name() + " is equal to " +
                 instrsWithEqualOpPart.at(*opPart).at(0)->name();
          throw std::runtime_error(err.toStdString().c_str());
        }
        instrsWithEqualOpPart[*opPart].push_back(instr);
      }
    }

    MatchNode node(matcher);
    for (const auto &iter : instrsWithEqualOpPart) {
      bool isUniqueIdentifiable = false;
      if (iter.second.size() == 1) {
        auto &instr = iter.second[0];
        const size_t nOpParts = instr->getOpcode().opParts.size();
        if (fieldDepth == nOpParts || instr->hasExtraMatchConds()) {
          // End of opParts, uniquely identifiable instruction
          MatchNode child(iter.first);
          // At this point, the opPart at this level for the instruction is
          // invalid, and we must match on extra conditions.
          if (fieldDepth > nOpParts) {
            assert(instr->hasExtraMatchConds());
            child.matchOnExtraMatchConds();
          }
          child.instruction = instr;
          node.children.push_back(child);
          isUniqueIdentifiable = true;
        } else {
          // More opParts available; need to match on these as well. It might be
          // that different instructions alias across non-opcode fields such as
          // immediates or registers. If the ISA is valid, matching on all
          // opcode parts should yield the correct instruction.
        }
      }

      if (!isUniqueIdentifiable) {
        // Match branch; recursively continue match tree
        node.children.push_back(
            buildMatchTree(iter.second, fieldDepth + 1, iter.first));
      }
    }

    return node;
  }

  MatchNode m_matchRoot;
};

} // namespace Assembler
} // namespace Ripes
