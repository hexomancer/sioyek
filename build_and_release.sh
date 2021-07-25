mkdir -p linux-deploy-binaries
cd linux-deploy-binaries
curl -L https://github.com/probonopd/linuxdeployqt/releases/download/continuous/linuxdeployqt-continuous-x86_64.AppImage
chmod +x linuxdeployqt-continuous-x86_64.AppImage
cd ..

cd mupdf
make USE_SYSTEM_HARFBUZZ=yes
cd ..
qmake pdf_viewer_build_config.pro
rm -r sioyek-release 2> /dev/null
make install INSTALL_ROOT=sioyek-release

./linux-deploy-binaries/linuxdeployqt-continuous-x86_64.AppImage sioyek-release/usr/share/applications/sioyek.desktop -appimage

cp pdf_viewer/prefs.config sioyek-release/usr/bin/prefs.config
cp pdf_viewer/prefs_user.config sioyek-release/usr/share/prefs_user.config
cp pdf_viewer/keys.config sioyek-release/usr/bin/keys.config
cp pdf_viewer/keys_user.config sioyek-release/usr/share/keys_user.config
cp -r pdf_viewer/shaders sioyek-release/usr/bin/shaders
cp tutorial.pdf sioyek-release/usr/bin/tutorial.pdf

zip -r sioyek-release.zip sioyek-release
