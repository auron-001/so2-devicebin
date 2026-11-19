
# standoff 2 external devicebin
usually updated the same day the game updates

## how to use

1. download bluestacks (this specific version):
   [BlueStacks 5.22.130.1019](https://ak-build.bluestacks.com/public/app-player/windows/nxt/5.22.130.1019/03f0020b47ac3b5affd5d7ea53661c44/BlueStacksMicroInstaller_5.22.130.1019_native.exe?filename=BlueStacksInstaller_5.22.130.1019_native_e0175934c4a0a56f1db683585ffd1bcf.exe)

2. connect via ADB and transfer the token file to the Bluestacks window.

3. run the following commands:
```
   /system/xbin/bstk/su
   cp /sdcard/DCIM/SharedFolder/device /data/local/tmp/device
   chmod 777 /data/local/tmp/device
```

## variants of usage 

1. Capturing Verification/AppVerification via login:
```
/data/local/tmp/device json
```

2. the same + android-playintegrity token, facebook, and device:
```
/data/local/tmp/device json full
```

3. universal capture via BoltPlayerApi.instance (Google/VK/guest):
```
/data/local/tmp/device json any
/data/local/tmp/device json any full
```

4. your own path instead of /data/local/tmp/so2_capture.json:
```
/data/local/tmp/device json -o /data/local/tmp/my.json
```

5. Diagnostics: live FacebookAuthRequest objects:
```
/data/local/tmp/device scan
/data/local/tmp/device scan watch
```

6. Dump Il2CppClass by RVA (defaults to FacebookAuthRequest):
```
/data/local/tmp/device typeinfo
/data/local/tmp/device typeinfo 0xCD21C40 FacebookAuthRequest
```

## how to download a file to your computer 
```
adb pull /data/local/tmp/so2_capture.json "path"
```

# Notice 
For Facebook usage it is better to first login than logout and start the file before pressing again "Continue as" button