// iOS sandbox setup. Keep this translation unit free of engine headers: UIKit's BOOL
// and the game's Bool declarations must not meet.
#import <Foundation/Foundation.h>
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include "Platform/IOSStartup.h"
#include "Platform/IOSLifecycle.h"
#include "Platform/IOSWindowDiagnostics.h"
#include "IOSUserDataMigration.h"

static void uncaughtIOSException(NSException *exception)
{
    fprintf(stderr, "[IOS_EXCEPTION] name=%s reason=%s\n", exception.name.UTF8String,
        exception.reason ? exception.reason.UTF8String : "");
    fflush(stderr);
}

static void normalIOSExit()
{
    fprintf(stderr, "[IOS_EXIT] normal process exit\n");
    fflush(stderr);
}

extern "C" bool ZH_IOS_Bootstrap()
{
    @autoreleasepool {
        NSFileManager *files = NSFileManager.defaultManager;
        NSURL *documents = [files URLsForDirectory:NSDocumentDirectory inDomains:NSUserDomainMask].firstObject;
        if (!documents)
            return false;
        NSURL *game = [documents URLByAppendingPathComponent:@"GameData" isDirectory:YES];
        NSURL *user = [documents URLByAppendingPathComponent:@"UserData" isDirectory:YES];
        for (NSURL *directory in @[game, [game URLByAppendingPathComponent:@"ZH_Generals" isDirectory:YES],
                                   user, [user URLByAppendingPathComponent:@"ReforgedArt" isDirectory:YES]]) {
            NSError *error = nil;
            if (![files createDirectoryAtURL:directory withIntermediateDirectories:YES attributes:nil error:&error]) {
                fprintf(stderr, "iOS: cannot create data folder: %s\n", error.description.UTF8String);
                return false;
            }
        }
        NSString *log = [documents.path stringByAppendingPathComponent:@"generals-stderr.log"];
        NSString *previous = [documents.path stringByAppendingPathComponent:@"generals-stderr-prev.log"];
        [files removeItemAtPath:previous error:nullptr];
        [files moveItemAtPath:log toPath:previous error:nullptr];
        if (!freopen(log.fileSystemRepresentation, "w", stderr))
            return false;
        setvbuf(stderr, nullptr, _IOLBF, 0);
        // Older starts could cache the desktop directory before UIKit bootstrap.
        // Move only missing entries; existing Documents choices are preserved.
        NSURL *support = [files URLsForDirectory:NSApplicationSupportDirectory inDomains:NSUserDomainMask].firstObject;
        if (support) {
            NSURL *legacy = [support URLByAppendingPathComponent:@"Command and Conquer Generals Zero Hour Data" isDirectory:YES];
            IOSUserData::Migration migration;std::string error;
            const bool migrated = IOSUserData::migrate(legacy.path.fileSystemRepresentation,user.path.fileSystemRepresentation,migration,error);
            fprintf(stderr,"[IOS_USER_DATA] migration_ok=%d moved_entries=%u kept_legacy_conflicts=%u skipped_links=%u\n",
                int(migrated),migration.moved,migration.kept,migration.links);
            if(!migrated) {fprintf(stderr,"[IOS_USER_DATA] migration failed; originals retained where not moved: %s\n",error.c_str());return false;}
        }
        NSSetUncaughtExceptionHandler(uncaughtIOSException);
        atexit(normalIOSExit);
        NSString *fonts = [[NSBundle.mainBundle.bundlePath stringByAppendingPathComponent:@"Fonts"] stringByAppendingString:@"/"];
        setenv("ZH_IOS_GAME_DATA", game.path.fileSystemRepresentation, 1);
        setenv("ZH_USER_DATA_DIR", user.path.fileSystemRepresentation, 1);
        setenv("ZH_IOS_FONTS", fonts.fileSystemRepresentation, 1);
        NSString *guide = @"Zero Hour Reforged for iOS\n\n"
            "Copy Zero Hour files into GameData. Copy original Generals files into GameData/ZH_Generals.\n"
            "Both games are required. Keep .big files directly in those folders.\n"
            "Optional Reforged*.big art archives belong in UserData/ReforgedArt.\n"
            "Saves, options and logs are in UserData. Startup output is in generals-stderr.log.\n\n"
            "Controls: tap = left click; drag = selection.\n"
            "Hold still for 0.55 seconds and release, or tap with two fingers = right click.\n"
            "With two fingers, the first finger sets the click target.\n"
            "Two-finger drag = camera pan; pinch = zoom.\n"
            "Lift both fingers before switching between panning and pinching.\n"
            "Three-finger tap = Escape/menu. The minimap also moves the camera.\n";
        [guide writeToURL:[documents URLByAppendingPathComponent:@"READ-ME.txt"] atomically:YES
               encoding:NSUTF8StringEncoding error:nullptr];
        ZH_IOS_InstallWindowDiagnostics();
        SDL_SetAppMetadata("Zero Hour Reforged", "0.1", "io.github.roundedspine.reforged.ios");
        SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
        // The RTS gesture adapter delivers mouse events directly; avoid duplicate emulated clicks.
        SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
        fprintf(stderr, "[IOS_STARTUP] game_data=%s user_data=%s fonts=%s\n",
                getenv("ZH_IOS_GAME_DATA"), getenv("ZH_USER_DATA_DIR"), getenv("ZH_IOS_FONTS"));
        fprintf(stderr, "[IOS_BUILD] revision=%s renderer=SDL_GPU_Metal\n", ZH_IOS_REVISION);
        ZH_IOS_StartupTrace("bootstrap.ready", "Documents and stderr configured");
        if (!ZH_IOS_InstallLifecycle()) {
            fprintf(stderr, "iOS: cannot install application lifecycle watch: %s\n", SDL_GetError());
            return false;
        }
        return true;
    }
}
