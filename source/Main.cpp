#include <MainApplication.hpp>

int main(int argc, char **argv) {
    // hbloader passes the launched .nro path as argv[0]; the self-updater uses
    // it so it overwrites the file you actually ran (wherever it lives).
    MainApplication::SetLaunchPath((argc > 0 && argv[0]) ? argv[0] : "");

    auto opts = pu::ui::render::RendererInitOptions(
        SDL_INIT_EVERYTHING, pu::ui::render::RendererHardwareFlags);
    opts.UseImage(pu::ui::render::ImgAllFlags);

    // Use the console's shared fonts (covers Latin + CJK etc. — the i18n win).
    opts.SetPlServiceType(PlServiceType_User);
    opts.AddDefaultAllSharedFonts();

    opts.SetInputPlayerCount(1);
    opts.AddInputNpadStyleTag(HidNpadStyleSet_NpadStandard);
    opts.AddInputNpadIdType(HidNpadIdType_Handheld);
    opts.AddInputNpadIdType(HidNpadIdType_No1);

    auto renderer = pu::ui::render::Renderer::New(opts);
    auto main = MainApplication::New(renderer);

    const auto rc = main->Load();
    if (R_FAILED(rc)) {
        diagAbortWithResult(rc);
    }
    main->Show();
    main->Shutdown();
    return 0;
}
