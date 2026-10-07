project(tests)
ecm_add_test(StartupTelemetry_test.cpp ../launcher/awake/StartupTelemetry.cpp LINK_LIBRARIES Qt6::Core Qt6::Network Qt6::Test TEST_NAME StartupTelemetry)
target_include_directories(StartupTelemetry PRIVATE ${CMAKE_SOURCE_DIR}/launcher)

ecm_add_test(AwakeUpdateChecker_test.cpp ../launcher/updater/AwakeUpdateChecker.cpp LINK_LIBRARIES Qt6::Core Qt6::Gui Qt6::Network Qt6::Test TEST_NAME AwakeUpdateChecker)
target_sources(AwakeUpdateChecker PRIVATE ../launcher/updater/ExternalUpdater.h)
target_include_directories(AwakeUpdateChecker PRIVATE ${CMAKE_SOURCE_DIR}/launcher)
ecm_add_test(AwakeArtwork_test.cpp LINK_LIBRARIES Qt6::Core Qt6::Gui Qt6::Test TEST_NAME AwakeArtwork)
target_include_directories(AwakeArtwork PRIVATE ${CMAKE_SOURCE_DIR}/launcher)
target_sources(AwakeArtwork PRIVATE ${CMAKE_SOURCE_DIR}/launcher/awake/InstanceArtwork.cpp ${CMAKE_SOURCE_DIR}/launcher/resources/backgrounds/backgrounds.qrc)

ecm_add_test(AwakeLocale_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME AwakeLocale)
ecm_add_test(AwakeTheme_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME AwakeTheme)
ecm_add_test(AwakeSearch_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME AwakeSearch)
ecm_add_test(AwakeLibrary_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME AwakeLibrary)
target_compile_definitions(AwakeLibrary PRIVATE AWAKE_TEST_JAVA_PATH="${Java_JAVA_EXECUTABLE}")
set_tests_properties(AwakeLibrary PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;AWAKE_FRONTEND=widgets" TIMEOUT 60)
add_test(NAME AwakeEmptyLibrary COMMAND AwakeLibrary emptyLibraryStartup -o -,txt)
set_tests_properties(AwakeEmptyLibrary PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;AWAKE_TEST_EMPTY_LIBRARY=1;AWAKE_FRONTEND=widgets" TIMEOUT 60)
if(WIN32)
    set_property(TEST AwakeLibrary APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
    set_property(TEST AwakeEmptyLibrary APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
endif()

ecm_add_test(FileSystem_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME FileSystem)

ecm_add_test(GZip_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME GZip)

ecm_add_test(GradleSpecifier_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME GradleSpecifier)

ecm_add_test(MojangVersionFormat_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME MojangVersionFormat)

ecm_add_test(Library_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME Library)

ecm_add_test(ResourceFolderModel_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME ResourceFolderModel)

ecm_add_test(ResourcePackParse_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME ResourcePackParse)

ecm_add_test(TexturePackParse_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME TexturePackParse)

ecm_add_test(DataPackParse_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME DataPackParse)

ecm_add_test(ShaderPackParse_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME ShaderPackParse)

ecm_add_test(WorldSaveParse_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME WorldSaveParse)

ecm_add_test(ParseUtils_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME ParseUtils)

ecm_add_test(Task_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME Task)

ecm_add_test(INIFile_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME INIFile)

ecm_add_test(JavaVersion_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME JavaVersion)

ecm_add_test(Packwiz_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME Packwiz)

ecm_add_test(Index_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME Index)

ecm_add_test(Version_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME Version)

ecm_add_test(ModrinthUrl_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME ModrinthUrl)

ecm_add_test(MetaComponentParse_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME MetaComponentParse)

ecm_add_test(CatPack_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME CatPack)


ecm_add_test(XmlLogs_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME XmlLogs)

ecm_add_test(Commandline_test.cpp LINK_LIBRARIES Launcher_logic Qt${QT_VERSION_MAJOR}::Test
    TEST_NAME Commandline)

if(Launcher_ENABLE_AWAKE_WEB)
    ecm_add_test(AwakeWebPolicy_test.cpp ../launcher/awake/web/AwakeWebPolicy.cpp
        LINK_LIBRARIES Qt6::Core Qt6::Test TEST_NAME AwakeWebPolicy)
    target_include_directories(AwakeWebPolicy PRIVATE ${CMAKE_SOURCE_DIR}/launcher)
    ecm_add_test(AwakeWebBridge_test.cpp LINK_LIBRARIES Launcher_logic Qt6::Test TEST_NAME AwakeWebBridge)
    set_tests_properties(AwakeWebBridge PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;AWAKE_FRONTEND=widgets" TIMEOUT 60)
    if(WIN32)
        set_property(TEST AwakeWebBridge APPEND PROPERTY ENVIRONMENT "QT_QPA_FONTDIR=$ENV{WINDIR}/Fonts")
        ecm_add_test(AwakeWebShell_test.cpp LINK_LIBRARIES Launcher_logic Qt6::Test Qt6::QuickWidgets TEST_NAME AwakeWebShell)
        set_tests_properties(AwakeWebShell PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 90)
    endif()
endif()

ecm_add_test(CurseForge_test.cpp LINK_LIBRARIES Launcher_logic Qt6::Test TEST_NAME CurseForge)
set_tests_properties(CurseForge PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;AWAKE_FRONTEND=widgets" TIMEOUT 300)

ecm_add_test(MicrosoftAuth_test.cpp LINK_LIBRARIES Launcher_logic Qt6::Test TEST_NAME MicrosoftAuth)
set_tests_properties(MicrosoftAuth PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;AWAKE_FRONTEND=widgets" TIMEOUT 120)
