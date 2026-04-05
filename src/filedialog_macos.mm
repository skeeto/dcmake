#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#include <string>
#include "icon_data.h"

void platform_set_icon(void *)
{
    @autoreleasepool {
        NSData *data = [NSData dataWithBytesNoCopy:(void *)icon_png
                                            length:icon_png_size
                                      freeWhenDone:NO];
        NSImage *icon = [[NSImage alloc] initWithData:data];
        [[NSApplication sharedApplication] setApplicationIconImage:icon];
    }
}

std::string platform_open_directory_dialog()
{
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:NO];
        [panel setCanChooseDirectories:YES];
        [panel setAllowsMultipleSelection:NO];

        if ([panel runModal] == NSModalResponseOK) {
            NSURL *url = [[panel URLs] objectAtIndex:0];
            return std::string([[url path] UTF8String]);
        }
    }
    return {};
}

std::string platform_open_file_dialog()
{
    @autoreleasepool {
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];

        if ([panel runModal] == NSModalResponseOK) {
            NSURL *url = [[panel URLs] objectAtIndex:0];
            return std::string([[url path] UTF8String]);
        }
    }
    return {};
}
#endif
