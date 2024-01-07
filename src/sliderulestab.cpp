#include "sliderulestab.h"
#include "processorhandler.h"
#include "ui_sliderulestab.h"

#include "rv_i_ext.h"

namespace Ripes {

SliderulesTab::SliderulesTab(QToolBar *toolbar, QWidget *parent)
    // TODO(raccog): Initialize default selected ISA using cached settings
    : RipesTab(toolbar, parent), ui(new Ui::SliderulesTab) {
  ui->setupUi(this);

  // Create encoding table and transfer ownership to UI layout.
  m_encodingTable = new EncodingView(*ui->isaSelector, *ui->regWidthSelector,
                                     *ui->mainExtensionSelector);
  ui->encodingLayout->addWidget(m_encodingTable);

  m_encodingTable->processorChanged();
}

SliderulesTab::~SliderulesTab() { delete ui; }

EncodingModel::EncodingModel(
    const std::shared_ptr<const ISAInfoBase> isa,
    const std::shared_ptr<const InstrVec> instructions,
    const std::shared_ptr<const PseudoInstrVec> pseudoInstructions,
    QObject *parent)
    : QAbstractTableModel(parent), isa(isa), instructions(instructions),
      pseudoInstructions(pseudoInstructions) {}

int EncodingModel::rowCount(const QModelIndex &) const {
  return instructions->size();
}
int EncodingModel::columnCount(const QModelIndex &) const { return BIT_END; }

QVariant EncodingModel::headerData(int section, Qt::Orientation orientation,
                                   int role) const {
  // Hide vertical header
  if (orientation != Qt::Horizontal) {
    return QVariant();
  }

  switch (role) {
  case Qt::DisplayRole: {
    switch (section) {
    case EXTENSION:
      return QString("Extension");
    case TYPE:
      return QString("Type");
    case DESCRIPTION:
      return QString("Description");
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

  return QVariant();
}

QVariant EncodingModel::data(const QModelIndex &index, int role) const {
  if (!index.isValid() ||
      static_cast<size_t>(index.row()) >= instructions->size()) {
    return QVariant();
  }

  auto row = index.row();
  auto col = index.column();

  if (col > BIT_END) {
    return QVariant();
  }

  // Check if this row contains an immediate field encoding
  auto immIdxIter = m_immediateRows.find(row);
  bool isImmediateRow = immIdxIter != m_immediateRows.end();

  if (isImmediateRow) {
    // This row contains an immediate field encoding.
    // TODO(raccog): Show encoding for constructed immediate here
  } else {
    // This row contains an instruction encoding.

    // Count how many rows before this one contain an immediate field encoding.
    auto prevImmediateIter = m_immediateRows.lower_bound(row);
    int prevImmediateRows =
        prevImmediateIter != m_immediateRows.end()
            ? std::distance(m_immediateRows.begin(), prevImmediateIter)
            : 0;

    auto &instr = instructions->at(row - prevImmediateRows);
    auto fields = instr->getFields();
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
        // Check if bit is used in a field.
        for (const auto &field : instr->getFields()) {
          for (const auto &range : field->ranges) {
            if (range.stop == bit) {
              return field->fieldType();
            }
          }
        }
        // Check if bit is used in an opcode part.
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

EncodingView::EncodingView(QComboBox &isaFamilySelector,
                           QComboBox &regWidthSelector,
                           QComboBox &mainExtensionSelector, QWidget *parent)
    : QTableView(parent), m_isaFamilySelector(isaFamilySelector),
      m_regWidthSelector(regWidthSelector),
      m_mainExtensionSelector(mainExtensionSelector) {
  // Add all supported ISA families into the ISA family selector.
  for (const auto &isaFamily : ISAFamilyNames) {
    m_isaFamilySelector.addItem(isaFamily.second);
  }

  connect(&m_isaFamilySelector, &QComboBox::currentIndexChanged, this,
          &EncodingView::isaFamilyChanged);
  connect(&m_regWidthSelector, &QComboBox::currentIndexChanged, this,
          &EncodingView::regWidthChanged);
  connect(&m_mainExtensionSelector, &QComboBox::currentIndexChanged, this,
          &EncodingView::mainExtensionChanged);

  connect(ProcessorHandler::get(), &ProcessorHandler::processorChanged, this,
          &EncodingView::processorChanged);
}

void EncodingView::processorChanged() {
  auto isaInfo = ProcessorHandler::currentISA();
  ISAFamily isaFamily = ISAFamilies.at(isaInfo->isaID());
  emit m_isaFamilySelector.currentIndexChanged(static_cast<int>(isaFamily));

  ISA isa = isaInfo->isaID();
  auto familySet = ISAFamilySets.at(isaFamily);
  auto isaResult = familySet.find(isa);
  assert(isaResult != familySet.end() && "Could not find ISA in ISAFamilySets");
  int idx = std::distance(familySet.begin(), isaResult);
  m_regWidthSelector.setCurrentIndex(idx);
}

void EncodingView::isaFamilyChanged(int index) {
  if (index < 0) {
    return;
  }

  ISAFamily isaFamily = static_cast<ISAFamily>(index);

  // Add ISA register width options to selector.
  if (m_regWidthSelector.count() > 0) {
    m_regWidthSelector.clear();
  }
  for (const auto &isa : ISAFamilySets.at(isaFamily)) {
    auto isaName = ISANames.at(isa);
    m_regWidthSelector.addItem(isaName);
  }
}

void EncodingView::regWidthChanged(int index) {
  if (index < 0) {
    return;
  }

  auto vec = ISAFamilySets.at(
      static_cast<ISAFamily>(m_isaFamilySelector.currentIndex()));
  auto iter = vec.begin();
  std::advance(iter, index);
  assert(iter != vec.end() && "Could not find ISA in ISAFamilySets");
  ISA isa = *iter;
  auto isaInfo = ISAInfoRegistry::getISA(isa, QStringList());

  disconnect(&m_mainExtensionSelector, &QComboBox::currentIndexChanged, this,
             &EncodingView::mainExtensionChanged);
  if (m_mainExtensionSelector.count() > 0) {
    m_mainExtensionSelector.clear();
  }
  m_mainExtensionSelector.addItem(isaInfo->baseExtension());
  for (const auto &extName : isaInfo->supportedExtensions()) {
    m_mainExtensionSelector.addItem(extName);
  }
  m_mainExtensionSelector.setCurrentIndex(0);
  connect(&m_mainExtensionSelector, &QComboBox::currentIndexChanged, this,
          &EncodingView::mainExtensionChanged);

  if (!m_model || m_model->isa->isaID() != isa) {
    updateModel(isaInfo);
  }
}

void EncodingView::mainExtensionChanged(int index) {
  if (index < 0) {
    return;
  }

  auto selectedExtension = m_mainExtensionSelector.currentText();
  auto extensions = m_model->isa->baseExtension() != selectedExtension
                        ? QStringList(selectedExtension)
                        : QStringList();
  auto isaInfo = ISAInfoRegistry::getISA(m_model->isa->isaID(), extensions);
  updateModel(isaInfo);
}

void EncodingView::updateModel(std::shared_ptr<const ISAInfoBase> isa) {
  if (!m_model || !m_model->isa->eq(isa.get(), isa->enabledExtensions())) {
    m_model = std::make_unique<EncodingModel>(
        isa, std::make_shared<const InstrVec>(isa->instructions()),
        std::make_shared<const PseudoInstrVec>(isa->pseudoInstructions()));
    setModel(m_model.get());
    updateView();
  }
}

void EncodingView::updateView() {
  verticalHeader()->setVisible(false);
  horizontalHeader()->setMinimumSectionSize(30);
  resizeColumnsToContents();
  horizontalHeader()->setSectionResizeMode(EncodingModel::DESCRIPTION,
                                           QHeaderView::ResizeMode::Stretch);
  clearSpans();
  int row = 0;
  const auto &mainSelectedExtension = m_mainExtensionSelector.currentText();
  for (const auto &instr : *m_model->instructions) {
    setRowHidden(row, false);
    for (const auto &field : instr->getFields()) {
      for (const auto &range : field->ranges) {
        if (range.width() > 1) {
          setSpan(row, EncodingModel::BIT_END - range.stop - 1, 1,
                  range.width());
        }
      }
    }
    if (mainSelectedExtension != m_model->isa->baseExtension() &&
        m_model->isa->baseExtension() == instr->extensionOrigin()) {
      setRowHidden(row, true);
    }
    ++row;
  }
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
