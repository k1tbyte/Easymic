#pragma once

//////////////////////////////////////////////////////////////////////////////
// Common resources (Icons, Sounds, Menus)
//////////////////////////////////////////////////////////////////////////////

// Icons
#define IDI_APP             4
#define IDI_MIC	            5
#define IDI_MIC_MUTED		6
#define IDI_MIC_UNMUTED		7
#define IDI_MIC_ACTIVE		8

// Sounds
#define IDR_MUTE            18
#define IDR_UNMUTE		    19
#define IDR_TICK            20
#define IDR_ENABLE          21
#define IDR_DISABLE         22

// Menus
#define IDR_TRAY_MENU	    100
#define ID_APP_EXIT         40009
#define ID_APP_SETTINGS     40010
/// Posted to self, never on a menu: closing the settings window may not destroy it from inside
/// its own WM_DESTROY, so the drop is deferred back through the command queue.
#define ID_APP_SETTINGS_CLOSED 40012


//////////////////////////////////////////////////////////////////////////////
// Settings Window (IDD_SETTINGS)
//////////////////////////////////////////////////////////////////////////////


#define IDD_SETTINGS_MAIN                           150
#define IDC_SETTINGS_TREE                           151
#define IDC_SETTINGS_GROUPBOX                       152
#define IDC_SETTINGS_VERSION                        153

#define IDD_SETTINGS_PAGE                           201
// The two Custom-row controls the page proc talks to by name - generated row ids start at 1000
#define IDC_HOTKEYS_LIST                            402
#define IDC_ABOUT_LOG_LIST                          504


#define IDD_ACTION_EDIT                              451
#define IDC_ACTION_NAME                              452
#define IDC_ACTION_COMMAND                           453
#define IDC_ACTION_HOTKEY                            454
#define IDC_ACTION_ON_RELEASE                        455
#define IDC_ACTION_DELETE                            456
#define IDC_ACTION_SOUND                             457
#define IDC_ACTION_SOUND_BROWSE                      458
#define IDC_ACTION_NAME_LABEL                        459
#define IDC_ACTION_COMMAND_LABEL                     460
#define IDC_ACTION_SOUND_LABEL                       461
#define IDC_ACTION_NOTIFICATION_LABEL                462
#define IDC_ACTION_NOTIFICATION                      463
#define IDC_ACTION_NOTIFICATION_ENABLED              464
#define IDC_ACTION_NOTIFICATION_TOKENS               465
#define IDC_ACTION_COMMAND_TOKENS                    466
#define IDC_ACTION_PRESSES_LABEL                     467
#define IDC_ACTION_PRESSES                           468
#define IDC_ACTION_BLOCK                             469
#define IDC_ACTION_SOUND_VOLUME_LABEL                470
#define IDC_ACTION_SOUND_VOLUME                      471
#define IDC_ACTION_TAP_ONLY                          472
#define IDC_ACTION_ARGS_LABEL                        473
#define IDC_ACTION_ARGS                              474
#define IDC_ACTION_APP_LABEL                         475
#define IDC_ACTION_APP                               476

#define IDD_UPDATE_DIALOG                            601
#define IDC_UPDATE_TITLE                             602
#define IDC_UPDATE_VERSION                           603
#define IDC_UPDATE_NOTES                             604
#define IDC_UPDATE_INSTALL                           605
#define IDC_UPDATE_SKIP                              606
#define IDC_UPDATE_LATER                             607

#define IDD_RULE_EDIT                                701
#define IDC_RULE_WORD                                702
#define IDC_RULE_MATCH_EXACT                         703
#define IDC_RULE_MATCH_STARTS                        704
#define IDC_RULE_MATCH_CONTAINS                      705
#define IDC_RULE_CASE                                706
#define IDC_RULE_ALWAYS                              707
#define IDC_RULE_NEVER                               708
#define IDC_RULE_NOTE                                709

