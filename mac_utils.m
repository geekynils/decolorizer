#include "mac_utils.h"

#import <AppKit/NSMenu.h>
#import <AppKit/NSApplication.h>

// Adapted from: https://github.com/floooh/sokol/pull/362#issuecomment-739002722
void initialize_mac_menu(const char *title) {
    NSMenu* menu_bar = [[NSMenu alloc] init];
    NSMenuItem* app_menu_item = [[NSMenuItem alloc] init];
    [menu_bar addItem:app_menu_item];
    NSApp.mainMenu = menu_bar;
    NSMenu* app_menu = [[NSMenu alloc] init];
    NSString* window_title_as_nsstring = [NSString stringWithUTF8String:title];
    // `quit_title` memory will be owned by the NSMenuItem, so no need to release it ourselves
    NSString* quit_title =  [@"Quit " stringByAppendingString:window_title_as_nsstring];
    NSMenuItem* quit_menu_item = [[NSMenuItem alloc]
        initWithTitle:quit_title
        action:@selector(terminate:)
        keyEquivalent:@"q"];
    
    NSString* hide_title =  [@"Hide " stringByAppendingString:window_title_as_nsstring];
    NSMenuItem* hide_menu_item = [[NSMenuItem alloc]
        initWithTitle:hide_title
        action:@selector(hide:)
        keyEquivalent:@"h"];
    
    NSMenuItem* hide_others_item = [[NSMenuItem alloc]
        initWithTitle:@"Hide Others"
        action:@selector(hideOtherApplications:)
        keyEquivalent:@"h"];
    
    [hide_others_item setKeyEquivalentModifierMask: NSEventModifierFlagOption];
    
    NSMenuItem* show_all_item = [[NSMenuItem alloc]
        initWithTitle:@"Show All"
        action:@selector(unhideAllApplications:)
        keyEquivalent:@""];
    
    [app_menu addItem:hide_menu_item];
    [app_menu addItem:hide_others_item];
    [app_menu addItem:show_all_item];
    [app_menu addItem:[NSMenuItem separatorItem]];
    [app_menu addItem:quit_menu_item];
    app_menu_item.submenu = app_menu;
    
    // TODO Figure out how to properly do the memory management
    /*
    [window_title_as_nsstring release];
    [app_menu release];
    [app_menu_item release];
    [menu_bar release];
     */
}
