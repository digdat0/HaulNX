#include <MainApplication.hpp>

int main(int argc, char **argv) {
    // hbloader passes the launched .nro path as argv[0]; the self-updater uses
    // it so it overwrites the file you actually ran (wherever it lives).
    MainApplication::SetLaunchPath((argc > 0 && argv[0]) ? argv[0] : "");

    // Mount romfs before the renderer initializes: the bundled fallback font
    // below is loaded from romfs during renderer init (OnLoad's romfsInit
    // then no-ops against the existing mount).
    romfsInit();

    auto opts = pu::ui::render::RendererInitOptions(
        SDL_INIT_EVERYTHING, pu::ui::render::RendererHardwareFlags);
    opts.UseImage(pu::ui::render::ImgAllFlags);

    // Use the console's shared fonts (covers Latin + CJK etc. — the i18n win).
    opts.SetPlServiceType(PlServiceType_User);
    // Vietnamese fallback: the Switch shared fonts lack Latin Extended
    // Additional (ế, ệ, ợ...). This subset contains ONLY those glyphs, so the
    // system font still renders everything it can (font paths are consulted
    // before shared fonts, hence the aggressive subsetting).
    opts.AddDefaultFontPath("romfs:/fonts/viet-fallback.ttf");
    opts.AddDefaultAllSharedFonts();
    // Tiny size for the queue cards' chip/filename (defaults start at 27).
    opts.AddExtraDefaultFontSize(21);

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
