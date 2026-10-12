#include "web_graphics.h"
#include <universal/q_shared.h>
#include <ui/ui_shared.h>
#include <ui/ui_main.h>
#include <gfx_d3d/r_material.h>
#include <cstring>

namespace
{
menuDef_t browserMenu{};
itemDef_s rows[7]{};
textDef_s texts[7]{};
focusItemDef_s focuses[7]{};
multiDef_s multis[4]{};
itemDef_s *rowPointers[7]{};
GenericEventScript closeScript{}, linkScript{};
GenericEventHandler escapeHandler{}, closeHandler{}, linkHandler{};
itemDef_s graphicsLink{};
textDef_s linkText{};
focusItemDef_s linkFocus{};
itemDef_s *graphicsItems[512]{};
const char *labels[] = {"Disable distortion", "Disable motion blur", "Reduce dynamic lights", "Warm shaders during loading"};
const char *dvars[] = {"web_disableDistortion", "web_disableMotionBlur", "web_reduceDynamicLights", "web_warmShaders"};
bool initialized = false;
char optionLabels[4][2][80]{};

void InitMenu()
{
    browserMenu.window.name = "kisak_browser_graphics";
    browserMenu.window.rect = {-235, -150, 470, 300, 2, 2};
    browserMenu.window.rectClient = browserMenu.window.rect;
    browserMenu.window.style = 1;
    browserMenu.window.staticFlags = 0x1000000;
    browserMenu.window.backColor[0] = browserMenu.window.backColor[1] = browserMenu.window.backColor[2] = 0.04f;
    browserMenu.window.backColor[3] = 0.98f;
    browserMenu.window.foreColor[3] = 1;
    browserMenu.window.background = Material_RegisterHandle((char *)"white", 3);
    browserMenu.focusColor[0] = 1; browserMenu.focusColor[1] = 0.7f; browserMenu.focusColor[3] = 1;
    browserMenu.disableColor[0] = browserMenu.disableColor[1] = browserMenu.disableColor[2] = 0.4f;
    browserMenu.disableColor[3] = 1;
    browserMenu.cursorItem[0] = -1;
    browserMenu.priority = 10;
    browserMenu.itemCount = 7;
    browserMenu.items = rowPointers;
    closeScript.action = "close self ;";
    escapeHandler.name = "onESC"; escapeHandler.eventScript = &closeScript;
    closeHandler.name = "action"; closeHandler.eventScript = &closeScript;
    browserMenu.onEvent = &escapeHandler;
    for (int i = 0; i < 7; ++i)
    {
        itemDef_s &row = rows[i];
        rowPointers[i] = &row;
        row.parent = &browserMenu;
        row.window.rectClient = {20, float(20 + i * 34), 430, 26, 0, 0};
        row.window.rect = row.window.rectClient;
        row.window.dynamicFlags[0] = 4;
        for (float &f : row.window.foreColor) f = 1;
        row.type = row.dataType = 1;
        row.typeData.textDef = &texts[i];
        texts[i].textscale = 0.25f;
        texts[i].textaligny = 20;
        texts[i].textTypeData.focusItemDef = &focuses[i];
        if (i == 0 || i == 5) row.window.staticFlags = 0x100000;
    }
    texts[0].text = "BROWSER GRAPHICS";
    texts[0].textscale = 0.32f;
    for (int i = 0; i < 4; ++i)
    {
        rows[i + 1].type = rows[i + 1].dataType = 10;
        rows[i + 1].dvar = dvars[i];
        texts[i + 1].text = labels[i];
        multis[i].count = 2;
        Com_sprintf(optionLabels[i][0], sizeof(optionLabels[i][0]), "%s: Off", labels[i]);
        Com_sprintf(optionLabels[i][1], sizeof(optionLabels[i][1]), "%s: On", labels[i]);
        multis[i].dvarList[0] = multis[i].dvarStr[0] = optionLabels[i][0];
        multis[i].dvarList[1] = multis[i].dvarStr[1] = optionLabels[i][1];
        multis[i].dvarValue[1] = 1;
        focuses[i + 1].focusTypeData.multi = &multis[i];
    }
    texts[5].text = "Warming prepares shaders to reduce stutter; adds loading time.";
    texts[5].textscale = 0.18f;
    texts[6].text = "BACK";
    rows[6].onEvent = &closeHandler;
    initialized = true;
}
}

void Web_EnsureGraphicsMenu(UiContext *dc, menuDef_t *menu)
{
    if (!menu || !menu->window.name || I_stricmp(menu->window.name, "options_graphics")) return;
    if (!initialized) InitMenu();
    if (!Menus_FindByName(dc, browserMenu.window.name) && dc->menuCount < 600)
        dc->Menus[dc->menuCount++] = &browserMenu;
    for (int i = 0; i < menu->itemCount; ++i)
        if (menu->items[i] == &graphicsLink) return;
    if (menu->itemCount >= 511) return;
    graphicsLink = {};
    graphicsLink.parent = menu;
    graphicsLink.window.name = "kisak_browser_graphics_link";
    graphicsLink.window.rectClient = {405, 430, 220, 24, 0, 0};
    graphicsLink.window.rect = graphicsLink.window.rectClient;
    graphicsLink.window.dynamicFlags[0] = 4;
    for (float &f : graphicsLink.window.foreColor) f = 1;
    graphicsLink.type = graphicsLink.dataType = 1;
    linkText = {};
    linkText.text = "BROWSER GRAPHICS";
    linkText.textscale = 0.25f;
    linkText.textaligny = 20;
    linkText.textTypeData.focusItemDef = &linkFocus;
    graphicsLink.typeData.textDef = &linkText;
    linkScript.action = "open kisak_browser_graphics ;";
    linkHandler.name = "action"; linkHandler.eventScript = &linkScript;
    graphicsLink.onEvent = &linkHandler;
    for (int i = 0; i < menu->itemCount; ++i) graphicsItems[i] = menu->items[i];
    graphicsItems[menu->itemCount++] = &graphicsLink;
    menu->items = graphicsItems;
}
