// Copyright 2022 DolphiniOS Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Adapted for Flycast

#import "JitManager.h"
#import "JitManager+Debugger.h"

typedef NS_ENUM(NSInteger, FlycastJitType) {
  FlycastJitTypeDebugger,
  FlycastJitTypeUnrestricted
};

@interface JitManager ()

@property (readwrite, assign) bool acquiredJit;
@property (readwrite, assign) bool deviceHasTxm;

@end

@implementation JitManager {
  FlycastJitType _jitType;
}

+ (JitManager*)shared {
  static JitManager* sharedInstance = nil;
  static dispatch_once_t onceToken;

  dispatch_once(&onceToken, ^{
    sharedInstance = [[self alloc] init];
  });

  return sharedInstance;
}

- (id)init {
  if (self = [super init]) {
#if TARGET_OS_SIMULATOR
    _jitType = FlycastJitTypeUnrestricted;
#else
    _jitType = FlycastJitTypeDebugger;
#endif
    
    self.acquiredJit = false;
    
    if (@available(iOS 26, *)) {
      self.deviceHasTxm = [self checkIfDeviceUsesTXM];
    } else {
      // This is technically untrue on some devices, but it only matters on iOS 26 or above.
      self.deviceHasTxm = false;
    }
  }
  
  return self;
}

- (void)recheckIfJitIsAcquired {
  if (_jitType == FlycastJitTypeDebugger) {
    if (self.deviceHasTxm) {
      NSDictionary* environment = [[NSProcessInfo processInfo] environment];
      
      if ([environment objectForKey:@"XCODE"] != nil) {
        static dispatch_once_t onceToken;

        dispatch_once(&onceToken, ^{
          self.acquisitionError = @"JIT cannot be enabled while running within Xcode on iOS 26.";
        });
        
        return;
      }
    }
    
    self.acquiredJit = [self checkIfProcessIsDebugged];
    
    if (self.deviceHasTxm && self.acquiredJit) {
      self.acquisitionError = @"A debugger is attached. However, if the debugger is not StikDebug, Flycast will crash when emulation starts.";
    }
  } else if (_jitType == FlycastJitTypeUnrestricted) {
    self.acquiredJit = true;
  }
}

@end
