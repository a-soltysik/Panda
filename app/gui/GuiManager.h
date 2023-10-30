#include "DevGui.h"
#include "UserGui.h"

namespace app
{

class GuiManager
{
public:
    GuiManager();

private:
    panda::utils::signals::BeginGuiRender::ReceiverT _beginGuiReceiver;
    DevGui _devGui;
    UserGui _userGui;
};
}
