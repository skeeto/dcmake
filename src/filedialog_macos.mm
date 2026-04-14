#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
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
std::string platform_save_file_dialog()
{
    @autoreleasepool {
        NSSavePanel *panel = [NSSavePanel savePanel];
        panel.allowedContentTypes =
            @[[UTType typeWithFilenameExtension:@"json"]];
        if ([panel runModal] == NSModalResponseOK) {
            return std::string([[[panel URL] path] UTF8String]);
        }
    }
    return {};
}

// Use the user's preferred language list (first entry) rather than
// CFLocaleCopyCurrent -- the locale can be "en_DE" (English speaker in
// Germany) while the UI language is Spanish, and we want the UI
// language.
std::string platform_language_code()
{
    @autoreleasepool {
        NSArray<NSString *> *langs = [NSLocale preferredLanguages];
        if ([langs count] == 0) return "en";
        const char *c = [[langs objectAtIndex:0] UTF8String];
        return c ? std::string(c) : "en";
    }
}
#endif
