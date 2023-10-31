#include "rv_c_ext.h"

namespace Ripes {
namespace RVISA {
namespace ExtC {

void enableExt(const ISAInfoBase *isa, InstrVec &instructions,
               PseudoInstrVec &) {
  using namespace TypeCA;
  using namespace TypeCI;
  using namespace TypeCSS;
  using namespace TypeCL;
  using namespace TypeCS;

  enableInstructions<CSub, CXor, COr, CAnd, CLwsp, CFldsp, CSlli, CLi, CLui,
                     CAddi16Sp, CAddi, CNop, CSwsp, CFsdsp, CLw, CSw, CFsd>(
      instructions);

  if (isa->bits() == 32) {
    enableInstructions<CFlwsp, CFswsp, CFlw, CFsw>(instructions);
  } else {
    enableInstructions<CLdsp, CAddiw, CSdsp, CLd, CSd>(instructions);
  }

  if (isa->bits() == 64) {
    enableInstructions<CSubw, CAddw>(instructions);
  }
}

} // namespace ExtC
} // namespace RVISA
} // namespace Ripes
