Place the vstsdk2.4 here, namely "pluginterfaces" and "public.sdk" folders.

Note for wine 64bit builds, the file `public.sdk/source/vst2.x/vstplugmain.cpp` needs to be changed, so that a few lines have:

```
VST_EXPORT VSTCALLBACK AEffect* ...
```

The `VSTCALLBACK` needs to be added, otherwise it will not work (due to call convention mismatch).
