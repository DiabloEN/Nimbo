#include "ui.h"
#include <windows.h>

bool Ui::animationsEnabled(){
    BOOL enabled=TRUE;
    return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&enabled,0) || enabled;
}
