#ifdef __APPLE__
#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#include <string>
#include "icon_data.h"

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

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

// NSApp follows the system appearance, read each frame.  Windows get an
// explicit appearance when the user picks one, which leaves NSApp alone.
void system_theme_watch()
{
}

bool system_dark()
{
    @autoreleasepool {
        NSAppearanceName name = [NSApp.effectiveAppearance
            bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua,
                                                NSAppearanceNameDarkAqua]];
        return [name isEqualToString:NSAppearanceNameDarkAqua];
    }
}

void platform_window_theme(void *window, bool dark)
{
    @autoreleasepool {
        NSWindow *w = glfwGetCocoaWindow((GLFWwindow *)window);
        w.appearance = [NSAppearance appearanceNamed:
            dark ? NSAppearanceNameDarkAqua : NSAppearanceNameAqua];
    }
}
#endif
