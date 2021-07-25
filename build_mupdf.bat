cd mupdf\platform\win32\
msbuild mupdf.sln /property:Configuration=Debug
msbuild mupdf.sln /property:Configuration=Release
ls mupdf\platform\win32\x64\Release
