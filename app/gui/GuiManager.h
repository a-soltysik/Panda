#include "DevGui.h"
#include "UserGui.h"

namespace app
{

class GuiManager
{
public:
    explicit GuiManager(const panda::Window& window);

private:
    panda::utils::signals::BeginGuiRender::ReceiverT _beginGuiReceiver;
    DevGui _devGui;
    UserGui _userGui;
};
}
