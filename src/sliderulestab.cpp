#include "sliderulestab.h"
#include "processorhandler.h"
#include "ui_sliderulestab.h"

#include "isa/rv32isainfo.h"
#include "rv_i_ext.h"

namespace Ripes {

SliderulesTab::SliderulesTab(QToolBar *toolbar, QWidget *parent)
    : RipesTab(toolbar, parent), ui(new Ui::SliderulesTab) {
  ui->setupUi(this);
}

SliderulesTab::~SliderulesTab() { delete ui; }

EncodingModel::EncodingModel(
    const std::shared_ptr<const ISAInfoBase> isa,
    const std::shared_ptr<const InstrVec> instructions,
    const std::shared_ptr<const PseudoInstrVec> pseudoInstructions,
    QObject *parent)
    : QAbstractTableModel(parent), m_isa(isa), m_instructions(instructions),
      m_pseudoInstructions(pseudoInstructions) {
  // Map instructions to their row index
  size_t row = 0;
  for (const auto &instr : *m_instructions) {
    m_rowInstrMap[row] = instr.get();
    ++row;
    bool hasImmediate = false;
    for (const auto &field : instr->getFields()) {
      // TODO(raccog): Better way of detecting immediate field
      if (field->fieldType() == "imm") {
        hasImmediate = true;
        break;
      }
    }
    // Add second row if instruction has immediates
    if (hasImmediate) {
      ++row;
    }
    // TODO(raccog): Add rows if instruction is larger than 32-bits
  }
  m_rows = row;
}

int EncodingModel::rowCount(const QModelIndex &) const { return m_rows; }
int EncodingModel::columnCount(const QModelIndex &) const { return BIT_END; }

QVariant EncodingModel::headerData(int section, Qt::Orientation orientation,
                                   int role) const {
  if (orientation == Qt::Horizontal) {
    switch (role) {
    case Qt::DisplayRole: {
      switch (section) {
      case EXTENSION:
        return QString("Extension");
      case TYPE:
        return QString("Type");
      case DESCRIPTION:
        return QString("Description");
      case EXPLANATION:
        return QString("Explanation");
      case OPCODE:
        return QString("Opcode");
      case FIELD0:
      case FIELD1:
      case FIELD2:
        return QString("Field ") + QString::number(section - FIELD0);
      }
      if (section >= BIT_START && section < BIT_END) {
        return QString::number(BIT_END - section - 1);
      }
    }
    }
  }

  return QVariant();
}

QVariant EncodingModel::data(const QModelIndex &index, int role) const {
  size_t row = static_cast<size_t>(index.row());
  if (index.row() < 0 || row >= m_rows) {
    return QVariant();
  }

  bool isImmediateRow = false;
  if (m_rowInstrMap.count(row) == 0) {
    assert(m_rowInstrMap.count(row - 1) == 1 &&
           "No matching instruction for row in encoding table");
    isImmediateRow = true;
  }
  auto col = index.column();
  auto &instr = m_rowInstrMap.at(isImmediateRow ? row - 1 : row);
  auto fields = instr->getFields();

  if (isImmediateRow) {
    // TODO(raccog): Show encoding for constructed immediate here
  } else {
    switch (role) {
    case Qt::DisplayRole: {
      switch (col) {
      case EXTENSION:
        return QString(instr->extensionOrigin());
      case TYPE:
        // TODO(raccog): Include an instruction's type in isainfo
        return "TODO";
      case DESCRIPTION:
        return QString(instr->description());
      case EXPLANATION:
        // TODO(raccog): Include an instruction's pseudocode in isainfo
        return QString("TODO");
      case OPCODE:
        return QString(instr->name());
      case FIELD0:
      case FIELD1:
      case FIELD2:
        if (fields.size() >= static_cast<size_t>(col - FIELD0) + 1)
          return QString(fields.at(col - FIELD0)->fieldType());
        break;
      }

      if (col >= BIT_START && col < BIT_END) {
        unsigned bit = static_cast<unsigned>(BIT_END - col - 1);
        for (const auto &field : instr->getFields()) {
          for (const auto &range : field->ranges) {
            if (range.stop == bit) {
              return field->fieldType();
            }
          }
        }
        for (const auto &opPart : instr->getOpParts()) {
          if (opPart.range.isWithinRange(bit)) {
            return QString(opPart.bitIsSet(bit) ? "1" : "0");
          }
        }
      }
      break;
    }
    }
  }
  switch (role) {
  case Qt::TextAlignmentRole:
    return Qt::AlignCenter;
  }

  return QVariant();
}

EncodingView::EncodingView(QWidget *parent) : QTableView(parent) {
  auto isa = ProcessorHandler::fullISA();
  updateModel(isa);
  updateView();

  connect(ProcessorHandler::get(), &ProcessorHandler::processorChanged, this,
          &EncodingView::isaChanged);
}

void EncodingView::isaChanged() {
  updateModel(ProcessorHandler::fullISA());
  updateView();
}

void EncodingView::updateModel(std::shared_ptr<const ISAInfoBase> isa) {
  m_model = std::make_unique<EncodingModel>(
      isa, std::make_shared<const InstrVec>(isa->instructions()),
      std::make_shared<const PseudoInstrVec>(isa->pseudoInstructions()));
  setModel(m_model.get());
}

void EncodingView::updateView() {
  verticalHeader()->setVisible(false);
  horizontalHeader()->setMinimumSectionSize(30);
  for (const auto &col :
       {EncodingModel::FIELD0, EncodingModel::FIELD1, EncodingModel::FIELD2}) {
    horizontalHeader()->setSectionResizeMode(
        col, QHeaderView::ResizeMode::ResizeToContents);
  }
  horizontalHeader()->setSectionResizeMode(EncodingModel::DESCRIPTION,
                                           QHeaderView::ResizeMode::Stretch);
  clearSpans();
  for (const auto &pair : m_model->m_rowInstrMap) {
    int row = static_cast<int>(pair.first);
    const auto &instr = pair.second;
    size_t fieldIdx = 0;
    for (const auto &field : instr->getFields()) {
      for (const auto &range : field->ranges) {
        if (range.width() > 1) {
          setSpan(row, EncodingModel::BIT_END - range.stop - 1, 1,
                  range.width());
        }
      }
      if (field->fieldType() == "imm") {
        setSpan(row, EncodingModel::FIELD0 + fieldIdx, 2, 1);
      }
      ++fieldIdx;
    }
  }
  resizeColumnsToContents();
}

DecodingModel::DecodingModel(const std::shared_ptr<const ISAInfoBase> isa,
                             const std::shared_ptr<const InstrVec> instructions,
                             QObject *parent)
    : QAbstractTableModel(parent), m_isa(isa), m_instructions(instructions) {}

int DecodingModel::rowCount(const QModelIndex &) const { return 0; }
int DecodingModel::columnCount(const QModelIndex &) const { return 0; }
QVariant DecodingModel::data(const QModelIndex &index, int role) const {
  return QVariant();
}

DecodingView::DecodingView(QWidget *parent) : QTableView(parent) {
  auto isa = ISAInfoRegistry::getISA<ISA::RV32I>(QStringList("M"));
  auto instructions = std::make_shared<InstrVec>();
  instructions->emplace_back(std::make_shared<RVISA::ExtI::TypeI::Addi>());
  m_model = std::make_unique<DecodingModel>(isa, instructions);
  setModel(m_model.get());
}

} // namespace Ripes
