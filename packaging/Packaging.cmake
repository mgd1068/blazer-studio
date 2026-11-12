# System-library DEB: Qt/fonts remain distribution packages, never bundled.
include(GNUInstallDirs)
install(FILES "${PROJECT_SOURCE_DIR}/packaging/blazer-studio.desktop" DESTINATION "${CMAKE_INSTALL_DATADIR}/applications")
install(FILES "${PROJECT_SOURCE_DIR}/assets/blazer-studio.svg" DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps")
install(FILES "${PROJECT_SOURCE_DIR}/packaging/blazer-studio.xml" DESTINATION "${CMAKE_INSTALL_DATADIR}/mime/packages")
set(blazer_documentation_dir "${CMAKE_INSTALL_DATADIR}/doc/blazer-studio")
install(FILES "${PROJECT_SOURCE_DIR}/README.md" "${PROJECT_SOURCE_DIR}/LICENSE"
    "${PROJECT_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
    DESTINATION "${blazer_documentation_dir}" COMPONENT Documentation)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/docs" "${PROJECT_SOURCE_DIR}/assets"
    "${PROJECT_SOURCE_DIR}/LICENSES"
    DESTINATION "${blazer_documentation_dir}" COMPONENT Documentation)
foreach(entry START TEST_PROTOCOL KNOWN_LIMITATIONS)
    file(GENERATE OUTPUT "${PROJECT_BINARY_DIR}/documentation/${entry}.md"
        CONTENT "# Blazer Studio ${BLAZER_RELEASE_LABEL}\n\n[${entry} öffnen](docs/beta/${entry}.md).\n\nDie vollständige Dokumentation und Abbildungen gehören zu diesem Kandidaten.\n")
    install(FILES "${PROJECT_BINARY_DIR}/documentation/${entry}.md"
        DESTINATION "${blazer_documentation_dir}" COMPONENT Documentation)
endforeach()
install(FILES "${PROJECT_SOURCE_DIR}/tools/check-doc-links.py"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/blazer-studio/tools")
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_NAME "blazer-studio")
set(CPACK_PACKAGE_VENDOR "Blazer Studio contributors")
set(CPACK_PACKAGE_CONTACT "https://github.com/mgd1068/blazer-studio/issues")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/mgd1068/blazer-studio")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Native Linux laser preparation and simulator - beta candidate")
set(CPACK_PACKAGE_VERSION "${BLAZER_RELEASE_LABEL}")
set(CPACK_DEBIAN_PACKAGE_VERSION "${BLAZER_DEBIAN_VERSION}")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Blazer Studio contributors")
set(CPACK_DEBIAN_PACKAGE_SECTION "graphics")
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-dialogs, qml6-module-qtquick-window, qml6-module-qtquick-templates, qml6-module-qtqml-workerscript, qml6-module-qtqml-models, fonts-dejavu-core, python3, qt6-qpa-plugins, qt6-wayland, qt6-svg-plugins, qt6-image-formats-plugins, shared-mime-info, hicolor-icon-theme, xdg-utils")
set(CPACK_DEBIAN_PACKAGE_SUGGESTS "inkscape, python3-venv")
set(CPACK_PACKAGING_INSTALL_PREFIX "/usr")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
include(CPack)
