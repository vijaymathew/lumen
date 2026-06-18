#include "input/InputController.h"

InputController::InputController(QObject *parent)
    : QObject(parent)
{
}

void InputController::setMode(Mode mode)
{
    if (m_mode == mode)
        return;
    m_mode = mode;
    emit modeChanged(m_mode);
}
