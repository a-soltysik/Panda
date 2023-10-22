#include "DevGui.h"
#include "UserGui.h"

namespace app
{

class GuiManager
{
public:
    GuiManager();

private:
    panda::utils::Signals::BeginGuiRender::ReceiverT _beginGuiReceiver;
    DevGui _devGui;
    [[maybe_unused]] UserGui _userGui;
};
}
