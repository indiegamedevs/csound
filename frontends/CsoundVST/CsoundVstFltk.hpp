/*
 * C S O U N D   V S T
 *
 * A VST plugin version of Csound, with Python scripting.
 *
 * L I C E N S E
 *
 * This software is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef CSOUNDVSTFLTK_H
#define CSOUNDVSTFLTK_H

class CsoundVstFltk;

#include <AEffEditor.hpp>
#include <FL/Fl_Help_View.H>
#include <FL/Fl_Pack.H>
#include <FL/Fl_Tabs.H>
#include <FL/Fl_Input.H>
#include <FL/Fl_Preferences.H>
#include <FL/Fl_Browser.H>
#include <FL/Fl_Text_Editor.H>
#include <FL/Fl_Text_Display.H>
#include <FL/Fl_Text_Buffer.H>
#include <FL/Fl_Button.H>
#include <FL/Fl_Check_Button.H>
#include <FL/Fl_Group.H>
#include <list>
#undef KeyPress
#include "CsoundVST.hpp"
#include "CsoundVstUi.h"

#if defined(WIN32)

extern HINSTANCE hInstance;

#endif

class WaitCursor
{
  void *cursor;
public:
  WaitCursor();
  virtual ~WaitCursor();
};

class CsoundVstFltk :
  public AEffEditor
{
public:
  void *windowHandle;
  Fl_Window *csoundVstUi;
  CsoundVST *csoundVST;
  int useCount;
  static std::string aboutText;
  static Fl_Preferences preferences;
  typedef enum {
    kEditorWidth = 610,
    kEditorHeight = 430,
    xPad = 4,
    yPad = 4
  } AEffEditorSize;
  Fl_Pack *mainPack;
  Fl_Tabs *mainTabs;
  Fl_Input *commandInput;
  Fl_Group *runtimeMessagesGroup;
  Fl_Browser *runtimeMessagesBrowser;
  Fl_Text_Editor *orchestraTextEdit;
  Fl_Text_Buffer *orchestraTextBuffer;
  Fl_Text_Editor *scoreTextEdit;
  Fl_Text_Buffer *scoreTextBuffer;
  Fl_Text_Editor *scriptTextEdit;
  Fl_Text_Buffer *scriptTextBuffer;
  Fl_Input *settingsEditSoundfileInput;
  Fl_Check_Button* settingsVstPluginModeEffect;
  Fl_Check_Button* settingsVstPluginModeInstrument;
  Fl_Check_Button* settingsCsoundPerformanceModeClassic;
  Fl_Check_Button* settingsCsoundPerformanceModePython;
  Fl_Check_Button* autoPlayCheckButton;
  Fl_Text_Buffer *aboutTextBuffer;
  Fl_Text_Display *aboutTextDisplay;
  Fl_Group *orchestraGroup;
  Fl_Group *scoreGroup;
  Fl_Group *scriptGroup;
  std::list<std::string> messages;
  std::string helpFilename;
  std::string messagebuffer;
  CsoundVstFltk(AudioEffect *audioEffect);
  virtual ~CsoundVstFltk(void);
  static void messageCallback(CSOUND *csound, int attribute, const char *format, va_list valist);
  virtual void updateCaption();
  virtual void updateModel();
  //    AEffEditor overrides.
  virtual long getRect(ERect **rect);
  virtual long open(void *windowHandle);
  virtual void close();
  virtual void idle();
  virtual void update();
  virtual void postUpdate();
  void onPerformScriptButtonThreadRoutine();
  // FLTK event handlers.
  void onNew(Fl_Button*, CsoundVstFltk* csoundVstFltk);
  void onNewVersion(Fl_Button*, CsoundVstFltk* csoundVstFltk);
  void onOpen(Fl_Button*, CsoundVstFltk* csoundVstFltk);
  void onImport(Fl_Button*, CsoundVstFltk* csoundVstFltk);
  void onSave(Fl_Button*, CsoundVstFltk* csoundVstFltk);
  void onSaveAs(Fl_Button*, CsoundVstFltk* csoundVstFltk);
  void onPerform(Fl_Button*, CsoundVstFltk* csoundVstFltk);
  void onStop(Fl_Button*, CsoundVstFltk* csoundVstFltk);
  void onEdit(Fl_Button*, CsoundVstFltk* csoundVstFltk);
  void onSettingsVstPluginMode(Fl_Check_Button*, CsoundVstFltk* csoundVstFltk);
  void onSettingsVstInstrumentMode(Fl_Check_Button*, CsoundVstFltk* csoundVstFltk);
  void onSettingsCsoundPerformanceModeClassic(Fl_Check_Button*, CsoundVstFltk* csoundVstFltk);
  void onSettingsCsoundPerformanceModePython(Fl_Check_Button*, CsoundVstFltk* csoundVstFltk);
  void onSettingsApply(Fl_Button*, CsoundVstFltk* csoundVstFltk);
  void onAutoPlayCheckButton(Fl_Check_Button*, CsoundVstFltk* csoundVstFltk);
  void onPerformWithoutExportCheckButton(Fl_Check_Button*, CsoundVstFltk* csoundVstFltk);
  void fltklock();
  void fltkunlock();
  void fltkflush();
  void fltkwait();
};

#endif

