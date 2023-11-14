#include "sliderulestab.h"
#include "ui_sliderulestab.h"

#include "processorhandler.h"

namespace Ripes {

static constexpr const size_t TEXT_COLS = 10;

namespace Cells {

struct Extension final : public CellStructure {
  Extension(size_t columnIndex, size_t columnCount)
      : CellStructure(columnIndex, columnCount) {
    m_sizeHint = SMALL_SIZE;
  }
  QVariant getVariant(const InstructionBase *instr, int role) const override {
    switch (role) {
    case Qt::DisplayRole:
      return instr->extensionOrigin();
    default:
      return CellStructure::getVariant(instr, role);
    }
  }
};
struct Type final : public CellStructure {
  Type(size_t columnIndex, size_t columnCount)
      : CellStructure(columnIndex, columnCount) {
    m_sizeHint = SMALL_SIZE;
  }
  QVariant getVariant(const InstructionBase *instr, int role) const override {
    switch (role) {
    case Qt::DisplayRole:
      // TODO(raccog): Get type from instruction
      return "TYPE";
    default:
      return CellStructure::getVariant(instr, role);
    }
  }
};
struct Description final : public CellStructure {
  Description(size_t columnIndex, size_t columnCount)
      : CellStructure(columnIndex, columnCount) {
    m_sizeHint = LARGE_SIZE;
  }
  QVariant getVariant(const InstructionBase *instr, int role) const override {
    switch (role) {
    case Qt::DisplayRole:
      // TODO(raccog): Get description from instruction
      return "DESCRIPTION";
    default:
      return CellStructure::getVariant(instr, role);
    }
  }
};
struct Explanation final : public CellStructure {
  Explanation(size_t columnIndex, size_t columnCount)
      : CellStructure(columnIndex, columnCount) {
    m_sizeHint = (columnIndex == 3) ? SMALL_SIZE : LARGE_SIZE;
  }
  QVariant getVariant(const InstructionBase *instr, int role) const override {
    switch (role) {
    case Qt::DisplayRole:
      // TODO(raccog): Get explanation from instruction
      return "EXPLANATION";
    default:
      return CellStructure::getVariant(instr, role);
    }
  }
};
struct Mnemonic final : public CellStructure {
  Mnemonic(size_t columnIndex, size_t columnCount)
      : CellStructure(columnIndex, columnCount) {
    m_sizeHint = LARGE_SIZE;
  }
  QVariant getVariant(const InstructionBase *instr, int role) const override {
    switch (role) {
    case Qt::DisplayRole:
      return instr->name();
    default:
      return CellStructure::getVariant(instr, role);
    }
  }
};
struct Fields final : public CellStructure {
  constexpr Fields(size_t columnIndex, size_t columnCount)
      : CellStructure(columnIndex, columnCount) {}
  QVariant getVariant(const InstructionBase *instr, int role) const override {
    switch (role) {
    case Qt::DisplayRole: {
      auto fields = instr->getFields();
      QString names;
      // Add each field's name to the table
      for (auto field = fields.begin(); field != fields.end();) {
        names += (*field)->fieldType();
        ++field;
        if (field != fields.end()) {
          names += ", ";
        }
      }
      return names;
    }
    default:
      return CellStructure::getVariant(instr, role);
    }
  }
};
struct Bits final : public CellStructure {
  Bits(size_t columnIndex, size_t columnCount)
      : CellStructure(columnIndex, columnCount) {
    m_sizeHint = BIT_SIZE;
  }
  QVariant getVariant(const InstructionBase *instr, int role) const override {
    switch (role) {
    case Qt::DisplayRole:
      if (instr->opPartBitIsSet(bitIdx())) {
        return QString::number(1);
      }
      // TODO(raccog): Properly set immediates
      // Set bitfield info
      for (const auto &field : instr->getFields()) {
        for (const auto &range : field->ranges) {
          unsigned start = m_columnCount - range.stop - 1;
          if (start == m_columnIndex) {
            return field->fieldType();
          }
        }
      }
      return QString::number(0);
    case Qt::BackgroundRole:
      if (instr->opPartInRange(bitIdx())) {
        return QBrush(Qt::red);
      } else {
        return QVariant();
      }
    default:
      return CellStructure::getVariant(instr, role);
    }
  }
};

template <typename... Structures>
void encodingStructure(
    std::vector<std::unique_ptr<Cells::CellStructure>> &encodingColumnStructure,
    size_t cols) {
  size_t i = 0;
  (
      [&] {
        encodingColumnStructure.emplace_back(
            std::make_unique<Structures>(i++, cols));
      }(),
      ...);
}

} // namespace Cells

SliderulesTab::SliderulesTab(QToolBar *toolbar, QWidget *parent)
    : RipesTab(toolbar, parent), ui(new Ui::SliderulesTab),
      m_isa(ProcessorHandler::currentISA()),
      m_instructions(std::make_shared<const InstrVec>(
          ProcessorHandler::getAssembler()->getInstructionSet())),
      m_pseudoInstructions(std::make_shared<const PseudoInstrVec>(
          ProcessorHandler::getAssembler()->getPseudoInstructionSet())),
      m_decodingModel(std::make_unique<DecodingModel>(
          ProcessorHandler::currentISA(), m_instructions,
          m_pseudoInstructions)),
      m_encodingModel(std::make_unique<EncodingModel>(
          ProcessorHandler::currentISA(), m_instructions,
          m_pseudoInstructions)) {
  ui->setupUi(this);
  // TODO(raccog): Enable filtering of instructions
  ui->instrFilterInput->setReadOnly(true);

  connect(ProcessorHandler::get(), &ProcessorHandler::processorChanged, this,
          &SliderulesTab::onProcessorChanged);

  updateTables();
}

void SliderulesTab::onProcessorChanged() {
  const std::shared_ptr<Assembler::AssemblerBase> assembler =
      ProcessorHandler::getAssembler();
  const ISAInfoBase *isa = ProcessorHandler::currentISA();
  InstrVec instructions = assembler->getInstructionSet();
  PseudoInstrVec pseudoInstructions = assembler->getPseudoInstructionSet();
  setData(isa, instructions, pseudoInstructions);
}

void SliderulesTab::setData(const ISAInfoBase *isa, InstrVec instructions,
                            PseudoInstrVec pseudoInstructions) {
  m_isa = isa;
  m_instructions = std::make_shared<const InstrVec>(instructions);
  m_pseudoInstructions =
      std::make_shared<const PseudoInstrVec>(pseudoInstructions);

  m_decodingModel = std::make_unique<DecodingModel>(m_isa, m_instructions,
                                                    m_pseudoInstructions);
  m_encodingModel = std::make_unique<EncodingModel>(m_isa, m_instructions,
                                                    m_pseudoInstructions);

  updateTables();
}

void SliderulesTab::updateTable(QTableView *table, ISAModel *model) {
  table->setModel(model);
  table->horizontalHeader()->setMinimumSectionSize(MIN_CELL_SIZE.width());
  table->resizeColumnsToContents();
  table->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeMode::Fixed);
  table->verticalHeader()->hide();
  table->show();
}

void SliderulesTab::updateTables() {
  updateTable(ui->decodingTable, m_decodingModel.get());
  updateTable(ui->encodingTable, m_encodingModel.get());

  ui->decodingTable->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeMode::Stretch);

  for (const auto i : {2, 4}) {
    ui->encodingTable->horizontalHeader()->setSectionResizeMode(
        i, QHeaderView::ResizeMode::Stretch);
  }
  for (size_t i = 0; i < m_instructions->size(); ++i) {
    // Set span of 3 columns for encoding explanation
    ui->encodingTable->setSpan(i, 3, 1, 3);
    // Set span of 3 columns for encoding fields
    ui->encodingTable->setSpan(i, 7, 1, 3);

    // TODO(raccog): Properly span immediates
    // Span all fields
    auto fields = m_instructions->at(i)->getFields();
    for (const auto &field : fields) {
      for (const auto &range : field->ranges) {
        unsigned start = m_encodingModel->columnCount() - range.stop - 1;
        ui->encodingTable->setSpan(i, start, 1, range.width());
      }
    }
  }
}

SliderulesTab::~SliderulesTab() { delete ui; }

ISAModel::ISAModel(
    const ISAInfoBase *isa, const std::shared_ptr<const InstrVec> instructions,
    const std::shared_ptr<const PseudoInstrVec> pseudoInstructions,
    QObject *parent)
    : QAbstractTableModel(parent), m_isa(isa), m_instructions(instructions),
      m_pseudoInstructions(pseudoInstructions) {}

ISAModel::~ISAModel() {}

void ISAModel::update() {
  using namespace Cells;
  size_t cols = this->columnCount();
  encodingStructure<Extension, Type, Description, Explanation, Explanation,
                    Explanation, Mnemonic, Fields, Fields, Fields>(
      m_encodingColumnStructure, cols);
  for (unsigned i = 0; i < m_isa->instrBits(); ++i) {
    m_encodingColumnStructure.emplace_back(
        std::make_unique<Bits>(TEXT_COLS + i, cols));
  }
}

int ISAModel::rowCount(const QModelIndex &) const {
  // TODO: Add pseudo instructions
  return m_instructions->size() /*+ m_pseudoInstructions->size()*/;
}

int ISAModel::columnCount(const QModelIndex &) const {
  return m_isa->instrBits() + TEXT_COLS;
}

EncodingModel::EncodingModel(
    const ISAInfoBase *isa, const std::shared_ptr<const InstrVec> instructions,
    const std::shared_ptr<const PseudoInstrVec> pseudoInstructions,
    QObject *parent)
    : ISAModel(isa, instructions, pseudoInstructions, parent) {
  update();
}

QVariant EncodingModel::instrData(size_t col, const InstructionBase *instr,
                                  int role) const {
  assert(col < m_encodingColumnStructure.size() && "Invalid column index");
  return m_encodingColumnStructure.at(col)->getVariant(instr, role);
}

QVariant EncodingModel::data(const QModelIndex &index, int role) const {
  size_t row = static_cast<size_t>(index.row());
  size_t col = static_cast<size_t>(index.column());
  assert(row < m_instructions->size() + m_pseudoInstructions->size() &&
         "Cannot index past sliderule encoding model");
  if (row < m_instructions->size()) {
    auto instr = m_instructions->at(row);
    return instrData(col, instr.get(), role);
  } else {
    if (role == Qt::DisplayRole) {
      if (col == 0) {
        return "PSEUDO";
      } else if (col == 6) {
        return m_pseudoInstructions->at(index.row() - m_instructions->size())
            ->name();
      }
    }
  }
  return QVariant();
}

DecodingModel::DecodingModel(
    const ISAInfoBase *isa, const std::shared_ptr<const InstrVec> instructions,
    const std::shared_ptr<const PseudoInstrVec> pseudoInstructions,
    QObject *parent)
    : ISAModel(isa, instructions, pseudoInstructions, parent) {}

QVariant DecodingModel::data(const QModelIndex &index, int role) const {
  if (role == Qt::DisplayRole) {
  }
  return QVariant();
}

} // namespace Ripes
