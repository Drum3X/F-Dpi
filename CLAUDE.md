# F-Dpi

İnternet sansürünü ve DPI engellerini aşmak için C++/Qt tabanlı masaüstü uygulaması.
[Throne](https://github.com/throneproj/Throne) (GPL-3.0) üzerine inşa edilmiştir:

- `upstream` remote'u Throne'un `dev` dalını gösterir.
- F-Dpi geliştirmesi `f-dpi/main` dalında yapılır.
- Upstream değişiklikleri `git fetch upstream && git merge upstream/dev` ile alınır. Throne dosyalarındaki değişiklikler bu yüzden küçük ve yerel tutulur; yeni kod mümkün olduğunca yeni dosyalarda yazılır (bkz. Konvansiyonlar).

## Gereksinimler (kaynak: kullanıcı — değiştirmeden önce sor)

### 1. Temel yapı ve kısıtlar
- **Temel:** Throne kod tabanı; Throne'un genel UI tasarımı korunur.
- **Teknoloji:** önyüzde C++ + Qt; arka planda `sing-box` (Throne Go core'u) ve `zapret` (`winws`).
- **Platform:** mimari cross-platform'a uygun tasarlanır, **şimdilik yalnızca Windows** derlenir.
- **Yetki:** F-Dpi doğrudan **Yönetici (Administrator)** yetkisiyle çalışır.
- **Karşılıklı dışlama:** Throne'daki "iki config aynı anda çalışmaz" mantığı korunur. **Proxy (sing-box)** ile **DPI Bypass (winws)** asla aynı anda çalışmaz; biri başlatılınca diğeri durdurulur.

### 2. Özellikler
1. **Genel ve ağ bazlı config sistemi**
   - Hem genel (varsayılan) hem ağa özel config oluşturulabilir.
   - Wi-Fi ve kablolu (Ethernet) ağlar tanınır.
   - Bağlanılan ağa atanmış özel config varsa otomatik o çalışır; yoksa genel config devreye girer.
2. **DPI bypass ve Blockcheck**
   - Zapret seçenekleri UI'dan manuel yapılandırılabilir.
   - **Blockcheck**, kullanıcının ağı için en uygun yöntemi otomatik bulur:
     - **Hızlı Tarama:** yaygın stratejiler, kullanıcıyı bekletmez.
     - **Derin Tarama:** tüm kombinasyonlar.
3. **Seçici / bölünmüş bypass (Split Bypass):** bypass yalnızca engelli sitelere uygulanabilir (hostlist / auto-hostlist) ya da belirli siteler hariç tutulabilir (exclude). Amaç, oyunlarda ve normal sitelerde sorun çıkmaması.
4. **Otomatik DNS (anti-DNS poisoning):** zehirlenmeyi aşmak için kullanıcı adına otomatik, hızlı ve güvenli DNS seçilir.

### 3. UI değişiklikleri (Throne üzerine)
1. Üstteki profil/grup alanı **Genel** ve **Özel (Ağ Bazlı)** olarak ikiye ayrılır.
2. Config ekleme/düzenleme ekranlarına **DPI Bypass** yapılandırma bölümü eklenir.

## Build (Windows x64)

Toolchain ve runtime binary'leri `deps/` altındadır (git-ignored). Yönetici yetkisi gerektirmez; tek sistem gereksinimi Visual Studio 2022 Build Tools (C++ workload).

```powershell
# 1) Toolchain + binary'ler (idempotent; -Force yeniden indirir, -Only zapret,singbox seçer)
powershell -ExecutionPolicy Bypass -File script\fetch_deps.ps1
# 2) Go core + Qt GUI + runtime paketleme -> deployment\windows-amd64\
powershell -ExecutionPolicy Bypass -File script\build_windows.ps1            # -SkipCore / -SkipGui / -Clean / -BuildType Debug
```

`fetch_deps.ps1` neleri getirir (SHA256'lar `deps\fetch_deps.log` dosyasına yazılır):

| Bileşen | Kaynak | Hedef |
|---|---|---|
| Qt 6.12.0 (MSVC, CI ile aynı prebuilt) | throneproj/buildqt | `deps/toolchain/Qt` |
| OpenSSL | throneproj/env_windows_legacy | `deps/toolchain/openssl` |
| Go (son stable), CMake, Ninja, protoc 31.1 + protoc-gen-go(-grpc) | resmi release'ler | `deps/toolchain/*` |
| sing-box (resmi, standalone) | SagerNet/sing-box | `deps/sing-box` |
| zapret-win-bundle (`winws.exe`, `WinDivert*`, `cygwin1.dll`, listeler, blockcheck) | bol-van/zapret-win-bundle | `deps/zapret` |
| Web dashboard, `srslist.h` | sing-box-dashboard, throneproj/routeprofiles | `res/dashboard`, `build/` |

Notlar:
- Go core'u ayrıca derlemek için `script/build_go.sh` kullanılır: Git Bash'te `GOOS=windows GOARCH=amd64 DEST=...` ile çalışır. `DEST`'i siler, bu yüzden `build_windows.ps1` onu `deployment/.core-windows-amd64`'e derleyip kopyalar.
- Deploy içinde `F-Dpi.exe`, `ThroneCore.exe`, `zapret/` alt dizini (`winws.exe` ve bağımlılıkları) ile `sing-box.exe` bulunur.
- **Smoke test (yükseltilmemiş shell'den):** `F-Dpi.exe` yönetici ister. UAC istemi çıkmadan pencere ve core'un açıldığını görmek için `$env:__COMPAT_LAYER="RunAsInvoker"` ayarlayıp `Start-Process` ile başlatın. Portable modda veri `deployment\windows-amd64\config\` altına yazılır; günlük dosyası `config\logs\throne.log`.
  - Günlük dosyası tamponludur: satırlar ancak sonraki bir yazımla diske düşer. Anlık durum için pencereyi okuyun.
  - **GUI'yi sürmek:** PowerShell'de UI Automation (`UIAutomationClient`) pencere metinlerini (durum çubuğu, tablo satırları, diyaloglar) okur.
    - Satırı seçip `SendKeys` ile `{ENTER}` göndermek profili başlatır; `^n` yeni profil diyaloğunu açar.
    - Profil diyaloğu ana pencerenin altında görünür; "Düzenle" adıyla ana pencerenin descendant'ları içinde aranır. Pencerenin sınıf adı oturumdan oturuma değişir, bu yüzden başlıkla eşleştirilir.
    - Qt açılır kutusu ve menüsü UIA `Select`/`Invoke` ile güvenilir çalışmaz; menü açık kalır. Bunun yerine `SetProcessDPIAware()` + `BoundingRectangle` merkezine `mouse_event` tıklaması, ardından `SendKeys` kullanılır. `SetFocus()` Qt odağını taşımaz.
    - Türkçe metin içeren `.ps1` dosyaları **BOM'lu UTF-8** kaydedilmelidir; PowerShell 5.1 aksi halde ANSI okur ve adlar eşleşmez.
    - Ekran görüntüsü `System.Drawing` `CopyFromScreen` ile alınır.
  - **Test verisi:** uygulama kapalıyken `config\throne.db` Node'un yerleşik `node:sqlite` modülüyle düzenlenebilir (`profiles` satırı + `groups.profiles_json`).
    - Ters bölü içeren JSON'u kabuk tırnaklaması yerine bir `.js` dosyasından ve `String.raw` ile yazın.
    - Elle eklenen satırların id'si `entity_ids.profile_last_id` sayacının altında kalmalıdır; yoksa uygulama yeni profile aynı id'yi verir ve satırın üzerine yazar.
  - Qt düğmelerinde UIA `Toggle`/`Invoke` `clicked` sinyalini tetiklemeyebilir (bölüm düğmeleri); fare tıklaması kullanın. Sekme bağlam menüsü sağ tıkla (`mouse_event` 8/16) açılır.
  - **UAC tuzağı:** Yönetici olarak çalışan bir oturum `windows_set_admin=true` kaydeder ve sonraki her normal başlatma kendini UAC ile yükseltir (Throne `RelaunchElevatedIfNeeded`). Yükseltilmemiş test öncesinde bu ayar `false` yapılır. Yükseltilmiş bir örnek normal shell'den kapatılamaz ve sürülemez.
  - **Yönetici testi:** tek UAC onayıyla `Start-Process powershell -Verb RunAs -File <betik>`; betik F-Dpi'yi başlatır (yetki devralınır), UIA ile sürer ve sonucu dosyaya yazar.
  - Yükseltilmemiş testte winws argümanları ve listeleri işler, sonra WinDivert'te exit 5 (erişim reddedildi) ile durur. DPI'nin gerçekten çalışması ve DPI→proxy geçişi ancak yönetici olarak test edilebilir.

### Çeviriler (Türkçe = `res/translations/tr_TR.ts`)
1. `build_windows.ps1 -SkipCore -UpdateTranslations` komutu lupdate ile tüm `.ts` dosyalarını kaynaktan yeniler.
2. **Yalnızca `tr_TR.ts` commit edilir.** zh_CN, fa_IR ve ru_RU dosyaları `git checkout -- res/translations/{zh_CN,fa_IR,ru_RU}.ts` ile geri alınır, çünkü upstream'e aittirler.
3. Yeni (unfinished) metinler `node script/ts_tool.js export res/translations/tr_TR.ts <dir> [chunks]` ile JSON parçalara bölünür. Çeviriler `<dir>/*.out.json` dosyalarına `{ "<id>": "metin" | ["%n form"] }` biçiminde yazılır ve `node script/ts_tool.js import res/translations/tr_TR.ts <dir>` ile geri yazılır. Durumu `node script/ts_tool.js stats …` gösterir.
4. Kurallar:
   - `%1`, `%n`, `%Ln`, `%s`, HTML etiketleri ve `\n` korunur.
   - Türkçede tek çoğul formu vardır.
   - Kullanıcıya görünen "Throne" adı Türkçede "F-Dpi" olur. `throne://` ve `thronectl` aynen kalır.
   - Terim sözlüğü: profil, grup, yönlendirme, çekirdek, abonelik, kural seti, giden/gelen, TUN modu, sistem proxy'si, kill switch.

## Mimari

### Motorlar ve karşılıklı dışlama
- **Proxy motoru = Throne Go core'u (`ThroneCore.exe`, `core/`).** sing-box'ı kütüphane olarak gömer; GUI ile named pipe (`throneIPC-<uuid>`) üzerinden, protobuf çerçeveleriyle konuşur (`core/gen/libcore.proto`, `src/api/RPC.cpp`). Tüm Throne özellikleri (test, istatistik, auto-selector) buna bağlıdır.
  - Standalone `sing-box.exe` paketlenir ama varsayılan runtime değildir; ileride yedek/teşhis amaçlı kullanılabilir.
- **DPI motoru = `winws.exe`** (zapret, WinDivert, ayrı süreç). `DpiSupervisor` tarafından yönetilir (bkz. "DPI motoru (F2 sonrası)").
  - winws bir cygwin build'idir: `cygwin1.dll` aynı dizinde olmalıdır.
- **Dışlama noktası (F2 ile uygulandı):** `ConnectionManager` (`src/engine/connection/ConnectionManager.cpp` + `ConnectionDpi.cpp`).
  - Ayrı bir arbiter sınıfı yoktur. **DPI profili de "çalışan profil"dir** (`m_running`), dolayısıyla Throne'un tek-aktif-profil kuralı (`m_running` + `mu_starting/mu_stopping`) iki motoru da kapsar: bir profil başlatılırken önce çalışan profil (proxy ya da DPI) `stop()` ile durdurulur.
  - Motor türü: `ConnectionManager::activeEngine()` → `ActiveEngine::{None, Proxy, Dpi}` (`EngineTypes.h`). Blockcheck (F5) ve NetworkMonitor (F4) bunu kullanacak.
  - `start()` profili çözdükten sonra `IsDpiProfile(ent)` ise `startDpi()`'ye dallanır: kill switch, auto-selector sıralaması, `BuildSingBoxConfig` ve core RPC'si atlanır.
  - `stop()`: DPI için RPC yerine `DpiSupervisor::stop()`. `crash=true` durdurmaları (core çökmesi/yeniden başlatması, "Restart Core") DPI'yi **durdurmaz**, çünkü winws core'a bağlı değildir.
  - `onCoreStarted`: yeniden başlayan core `started_id`'yi (çalışan DPI profili) devam ettirmek isterse winws yeniden başlatılmaz.
  - Auto-resume (`remember_id`) DPI profilini de normal yoldan başlatır. `restartForGuard`/`applyPendingRestart` da normal `start()`'tan geçer, yani dışlama korunur.
  - Core süreci DPI çalışırken boşta kalır (yüklü config yok, trafiğe dokunmaz).
  - **Kill switch açıkken DPI başlatılmaz** (tüm trafiği keserdi); açık hata mesajı verilir.
  - DPI başlarken sistem proxy'si açıksa OS proxy ayarı temizlenir (core'un inbound'u boştadır). `spmode_system_proxy` bayrağı korunur ve proxy'ye dönünce yeniden uygulanır.
  - `RunningProfiles.hpp` yalnızca edit-lock amaçlıdır; DPI için `{id}` yazılır.

### Önemli Throne alanları (analiz özeti)
**Başlat/durdur akışı**
- UI: `mainwindow_profile_lifecycle.cpp` (`profile_start/stop/toggle_connection`) → `EngineControl` (`include/engine/EngineControl.h`, impl `src/engine/EngineHost.cpp`) → `ConnectionManager::start/stop`.
- Durumlar: `ConnectionState`, `StartOutcome` (`include/engine/EngineTypes.h`).
- Core ölümü `Lifecycle::StopRunningProfile` üzerinden durdurmaya gider.

**DB (SQLiteCpp, `src/database/`)**
- Şema versiyonu yoktur.
- Kolon eklemek için her repo'nun `createTables()` fonksiyonuna `CREATE TABLE IF NOT EXISTS` ve `if (!xColumnExists) ALTER TABLE ... ADD COLUMN ... DEFAULT` eklenir.
- Tek seferlik veri migrasyonları `MarkersRepo` marker'ları + `DatabaseManager::applyMigrations()` ile yapılır.
- Profil tipe özgü ayarları `profiles.outbound_json` içinde JSON olarak tutar. Kolon listesi 4 yerde senkron tutulmalı (`ProfilesRepo` save/insert/load + `Database::execBatch*`).
- Yeni bir tablo:
  - kendi Repo'sunu alır (`MarkersRepo` örnek),
  - `DatabaseManager::initializeRepos()` içinde oluşturulur,
  - `Database.cpp` içindeki `kProfileTables` yedekleme listesine eklenir,
  - CMake kaynak listesine eklenir (dosyalar glob'lanmaz, açıkça listelenir).
- Ayar eklemek için `SettingsRepo.h` üyesine ve `initMaps()` girişine ekleme yapılır; gerekiyorsa `SettingsRegistry::HostKeys()` da güncellenir.

**Profil tipleri**
- Tipler `configs/outbounds/*` altında, `baseConfig`/`outbound` arayüzleriyle tanımlıdır; kayıt `src/configs/common/OutboundFactory.cpp` içindeki `NewOutboundByType` ve `RefusedForeignType` ile yapılır.
- DPI tipi için en yakın örnek `extracore.h`: harici süreç başlatıyor ve serverless.
- Yeni tip eklenirken dokunulacak yerler:
  - `Profile.h` accessor'ı
  - `generate.cpp` serverless listesi (~:501) ve extra-core kancaları
  - Proxy olmayan tipleri dışlayan listeler: `ScanProfiles.cpp`, `dialog_scan_instance.cpp`, `BackupArchive.cpp`, `mainwindow_connections.cpp`, `AutoSelectorPlan.cpp`

**Düzenleme UI'ı**
- `DialogEditProfile` (`src/ui/profile/dialog_edit_profile.cpp`) şunları kullanır:
  - `editorFactory()` hash'i (`makeEditor<EditX>`)
  - `setupTypeList()`
  - `serverless` seti
  - `ProfileEditor` arayüzü (`onStart/onEnd/bindValidation`)
- `edit_dpi` için `edit_extra_core.{h,cpp,ui}` kopyalanarak başlanır.

**Ana pencere**
- `include/ui/mainwindow.ui`:
  - üst satırda tool button'lar
  - `QSplitter` içinde `QTabWidget tabWidget`: her grup bir sekme; tek bir `ProfilesTableView` aktif sekmeye taşınır
- Sekme mantığı: `src/ui/mainWindow/mainwindow_groups.cpp` (`refresh_groups`, `show_group`, `show_group_tab_menu`).
- İndeks eşleme `MainWindowInternal.h` içindeki `tabIndex2GroupId/groupId2TabIndex` ile yapılır. Bu fonksiyonlar `GetGroupsTabOrder()` sırasını varsayar; Genel/Özel ayrımı sırasında `tabData` tabanlı hâle getirilmelidir.
- Sekme sırası `groups_order` tablosunda (`GroupsRepo`) tutulur.
- `on_tabWidget_*` slot'ları objectName'e bağlı otomatik bağlantılardır.

**DNS**
- Kaynak: `calculatePrerequisites()` içindeki `RouteDns::Current()` (`generate.cpp` ~:713).
- `buildDNSSection()` (~:1065) içinde `remote_dns`/`direct_dns` vardır; `upgradeUdpDnsToDoH` DoH eşlemesi yapar.
- Ayarlar: `SettingsRepo` (`remote_dns`, `direct_dns`, `use_dns_object`).
- UI: `RouteDnsEditor`.

**Yetki (F1 sonrası)**
- Windows'ta `F-Dpi.exe` **requireAdministrator** manifest'iyle derlenir. Qt ≥ 6.9 kendi manifest'ini gömüp `/MANIFESTUAC:NO` ile linkler, bu yüzden linker bayrağı işe yaramaz. Seviye `QT_WINDOWS_APP_PROJECT_EXECUTION_LEVEL` hedef özelliğiyle verilir (`CMakeLists.txt` içindeki "F-Dpi" bloğu).
- `FDPI_REQUIRE_ADMIN` derleme tanımı şunları yapar:
  - "Her Zaman Standart Kullanıcı Olarak Başlat" (`windows_no_admin`) ayarını gizler.
  - Autorun zamanlanmış görevini her zaman `HighestAvailable` yapar (`AutoRun.cpp::autoRunElevated`).
- Throne'un elevated relaunch'ı (`GuiStartup.cpp::RelaunchElevatedIfNeeded`) ve servis modu (`ServiceManager_win.cpp`) kodda durur. Süreç zaten yönetici olduğu için devreye girmezler.
- Installer hâlâ `PrivilegesRequired=lowest` (F7).

**Ağ izleme**
- Throne'un C++ tarafında yoktu; Go core'daki `core/internal/netmon` yalnızca core çalışırken kullanılabilir. F-Dpi kendi `Sys::NetworkMonitor`'ünü yazdı (bkz. "Genel / Özel config (F4 sonrası)").

**Markalama (F1 sonrası)**
- Tek kaynak: `include/global/Branding.hpp`. Şunları tanımlar:
  - `AppName` (görünen ad)
  - `AppId` (`setApplicationName`, veri dizinini belirler)
  - `InstancePrefix` (`fdpi-`)
  - `AutoRunTaskPrefix`
  - `HomepageUrl`
  - `UpdateRepo` (boşsa "Güncellemeleri Denetle" gizlenir)
  - upstream atfı (About'ta "Throne tabanlı").
- Exe adı: CMake hedefi hâlâ `Throne` (upstream merge kolaylığı için), Windows'ta `OUTPUT_NAME "F-Dpi"`. Go core'daki `parentcheck.go` Windows'ta ebeveyn olarak `F-Dpi.exe` bekler. Exe adı değişirse burası da değişmeli.
- Sürüm kaynağı (`cmake/windows/windows.cmake`) ve pencere başlığı F-Dpi. WebDAV varsayılan klasörü `F-Dpi`.
- Bilerek değiştirilmeyen iç tanımlayıcılar:
  - Veri ve süreç adları: `throne.db`, `throne.log`, `throneIPC`, `throne-tun`, `ThroneCore.exe`
  - Kaynak ve kod: `:/Throne` kaynak öneki, Go modülü `ThroneCore`
  - Bağlantılar: `throne://` (içe aktarma ve URL scheme kaydı), `thronectl`
- Hâlâ Throne adını taşıyan, Throne ile yan yana kurulumda çakışabilecek tanımlayıcılar (F7):
  - Kill switch WFP sağlayıcısı (`core/internal/guard/backend_windows.go`)
  - Headless servis adları (`HostPresence_win.cpp`, `ServiceManager_win.cpp`)
  - URL scheme ProgId (`UrlScheme.cpp`)
  - Installer (`script/windows_installer.iss`)
  - İkonlar (`res/Throne.ico`, `res/public/Throne.png`)
- İngilizce kaynak metinlerdeki "Throne" ifadelerine dokunulmaz, çünkü upstream çevirileri bozulur. Kullanıcıya görünen ad Türkçe çeviride "F-Dpi" olarak verilir.
- ⚠ Bu makinede `%APPDATA%\F-Dpi` ve `%LOCALAPPDATA%\F-Dpi` dizinleri bu repodan önce başka bir F-Dpi uygulaması tarafından oluşturulmuş (`profiles.json`, `settings.ini`, `fdpi.log`). Bizim uygulama portable modda bunlara dokunmaz. appdata modunda (`-appdata` / installer) aynı kök dizini paylaşır, ama dosya adları çakışmaz.

**Çeviriler**
- `res/translations/{zh_CN,fa_IR,ru_RU}.ts` upstream'e aittir.
- `tr_TR.ts` F-Dpi'ye aittir ve 3595 metnin tamamı çevrilidir.
- Dil seçimi: `dialog_basic_settings.ui` combo'sundaki 5. indeks; yükleyici `GuiStartup.cpp::LoadTranslator`. "Sistem" seçiliyken Türkçe Windows otomatik olarak `tr_TR` yükler.

### DPI motoru (F2 sonrası)
- **`dpi` profil tipi**: `include/configs/outbounds/dpi.h` + `src/configs/outbounds/dpi.cpp`.
  - Alanlar `outbound_json` içinde saklanır:
    - `preset` (boş = elle düzenlenmiş)
    - `strategy` (winws profil seçenekleri, `--new` ile ayrılır; `#` yorum, `%ZAPRET%` = winws dizini)
    - `wf_tcp`, `wf_udp`
    - `mode` (`all | hostlist | autohostlist`), `hostlist[]`, `exclude[]`
  - Yeni profil `Dpi::kDefaultPreset` ile başlar.
  - `outbound::IsDpi()` sanal kancası tipi tanıtır. `Build()` hata döner, çünkü proxy değildir; bu sayede zincir, yönlendirme ve test kullanımları reddedilir.
  - `Checks::DpiStrategy` dosya yazan veya F-Dpi'nin yönettiği seçenekleri reddeder (`--hostlist-auto`, `--wf-save`, `--debug=@`, `--pidfile`, `--daemon`).
  - `RefusedForeignType("dpi")`: link, abonelik ve API ile DPI profili içe aktarılmaz; winws yönetici olarak çalışır ve seçenekleri dosya okur.
  - Dışlandığı listeler: `generate.cpp` serverless, `ScanProfiles`, `dialog_scan_instance`, `mainwindow_connections` (yönlendirme hedefleri), `RouteRule` (unroutable), `AutoSelectorPlan` (`AutoSelectorSkip::Dpi`).
- **Preset'ler**: `include/dpi/DpiPresets.h`. `general` (zapret preset1 türevi), `split` ve `fake-ttl`.
- **Komut satırı**: `include/dpi/DpiLaunch.h`, `Dpi::BuildLaunch()`.
  - `--wf-tcp/--wf-udp` öne eklenir; strateji kendi `--wf-*` seçeneğini içeriyorsa eklenmez.
  - Split bypass seçenekleri her `--new` bloğuna eklenir.
  - Listeler `<config>/dpi/{hostlist,exclude,autohostlist}-<profilId>.txt` dosyalarına yazılır. Alan adları punycode ve küçük harfe çevrilir. autohostlist dosyası korunur, çünkü winws ona ekleme yapar.
  - `Dpi::WindowsCommandLine()`: cygwin, tırnaksız argümanlardaki `\` karakterini kaçış sayar. Bu yüzden ters bölü veya boşluk içeren değerler `--opt="C:\..."` biçiminde tırnaklanır ve `QProcess::setNativeArguments` ile verilir.
- **`DpiSupervisor`**: `include/engine/dpi/DpiSupervisor.h`, ConnectionManager'a aittir.
  - QProcess kendi thread'inde (`DS_dpi`) yaşar. `start()` yalnızca worker thread'den çağrılır ve 1,5 sn bekler, çünkü winws WinDivert veya argüman hatasında hemen kapanır. Bu sürede kapanırsa son çıktı satırıyla hata döner.
  - Bilinen WinDivert çıkış kodları açıklamalı mesaja çevrilir: 5 = yönetici gerekli, 2/3 = .sys eksik, 87 = filtre, 577/1275 = sürücü engellendi.
  - stdout `[DPI]` önekiyle log paneline gider.
  - Sonradan beklenmedik çıkış `unexpectedExit` sinyalini tetikler; ConnectionManager profili durdurur. `started_id` korunur.
  - `Sys::TieChildToSelf` (`include/sys/ChildProcess.hpp`, Windows'ta job object + `KILL_ON_JOB_CLOSE`) sayesinde F-Dpi çökerse winws de ölür. Açılışta `Sys::KillProcessesByPath` kalmış winws süreçlerini kapatır.
- **Düzenleme UI'ı (F3 sonrası)**: `EditDpi` (`src/ui/profile/edit_dpi.cpp`, kodla kurulmuş, `DialogEditProfile` içinde "DPI Bypass" kutusu).
  - Tip listesinde ilk sıradadır; Ctrl+N ile yeni profil varsayılan olarak DPI Bypass açılır.
  - **Strateji** sekmesi:
    - Hazır ayar, TCP/UDP portları ve strateji metni. Elle düzenleme hazır ayarı "Özel" yapar.
    - "Komut satırını göster": `Dpi::BuildLaunch(..., dryRun=true)` ile dosya yazmadan winws komutunu gösterir. Kaydedilmemiş profilin liste dosyası `*-new.txt` olarak görünür.
  - **Site seçimi** sekmesi (Split Bypass):
    - Mod: Tüm siteler / Yalnızca listedeki siteler / Otomatik (engellenenleri öğren).
    - "Siteler" listesi ve "Asla dokunma" listesi. Her birinde "Liste ekle" menüsü (`Dpi::BlockedSiteLists`: Discord, YouTube (zapret `list-youtube.txt`); `Dpi::ExcludedSiteLists`: oyun başlatıcıları/mağazaları) ve "Dosyadan içe aktar".
    - Otomatik modda "Öğrenilen: N site" + Göster/Temizle (`autohostlist-<id>.txt`).
    - Satırlar `Checks::DpiHost` ile doğrulanır (alan adı veya URL; IP reddedilir). Kayıtta `dpi::NormalizeHosts` uygulanır (punycode, küçük harf, tekrarsız).
    - Liste kutularının yüksekliği sınırlıdır (70–110 px); sekme değişince diyalog yeniden boyutlanır (1080p'de ~720 px).

### Genel / Özel config (F4 sonrası)
- **Veri:** Throne'un `groups` tablosu değişmedi.
  - Kapsam ayrı `network_scopes` tablosunda tutulur (`group_id` PK, `scope` = `general|network`, `networks_json`, `profile_id`).
  - Erişim `Configs::NetworkScopesRepo` (`dataManager->networkScopesRepo`) üzerindendir. Satırı olmayan grup geneldir.
  - Silinen grupların satırları yok sayılır; grup id'leri yeniden kullanılmaz.
  - Tablo yedekleme/geri yüklemede `kProfileTables` içindedir.
  - Diyalog yeni bir grubu id'siz düzenler: `SetPending(group, scope)` kapsamı grup `AddGroup` ile id alınca (sonraki ilk repo çağrısında) kaydeder. Bu sayede gruplar oluşturan 5 Throne çağrı noktasına dokunulmadı.
- **Ağ kimliği:** `Sys::NetworkMonitor` (`include/sys/NetworkMonitor.hpp`, Windows: `src/sys/windows/NetworkMonitor_win.cpp`, diğer platformlarda stub).
  - Varsayılan rota fiziksel adaptörün (Wi-Fi/Ethernet, en düşük IPv4 metrik, ağ geçidi olan) ağıdır; Wintun/TAP sayılmaz.
  - Bir ağın birden çok kimliği (`keys`) vardır, en kararlısı önce gelir:
    - `nlm:{GUID}`: Windows ağ profili, `INetworkListManager`.
    - `gw:<ağ geçidi MAC>`: `GetIpNetEntry2`.
    - `wifi:<SSID>`
  - Bağlı ağ, kimliklerinden herhangi biri eşleşirse tanınır.
  - **Windows 11 24H2, konum izni olmayan uygulamaya SSID'yi vermez** (WlanQueryInterface). Bu yüzden SSID'ye güvenilmez; ad olarak Windows ağ adı (NLM) gösterilir.
  - Değişiklik `NotifyIpInterfaceChange` ile (1,5 sn debounce) ve 20 sn'lik yoklamayla algılanır; algılama UI thread dışında çalışır.
  - Test kancası `FDPI_NETWORK_OVERRIDE_FILE`: ağı dosyadan okur (`wifi|key1,key2|Ad`). Yalnızca yoklamayla okunur, yani değişiklik ≤20 sn'de yansır.
- **Otomatik geçiş:** `Engine::NetworkSwitcher` (`src/engine/network/`, EngineHost'ta).
  - Hedef (`targetProfile(current)`): ağa bağlı Özel grubun seçili profili (yoksa ilk profili). Ağa bağlı grup yoksa ve çalışan/hatırlanan profil genel bir gruptaysa o profil kalır. Bu kural F5'te düzeltildi: önceden açılışta hatırlanan DPI profili son genel Socks ile değiştiriliyordu. Aksi halde `general_profile_id` (son başlatılan genel profil), o da yoksa ilk genel grubun ilk profili.
  - Yalnızca kullanıcı bir şey çalışmasını istiyorsa devreye girer: profil çalışıyorsa ya da `started_id >= 0` ise. Elle durdurma `started_id`'yi temizler, bu yüzden ağ değişimi bağlantıyı yeniden açmaz.
  - Başlatma `ConnectionManager::start` üzerinden yapılır, yani proxy/DPI dışlaması korunur.
  - Açılışta `EngineHost::init` içinde core başlamadan önce ağ eşzamanlı algılanır. Hatırlanan profil (`remember_id`) bu ağın hedefinden farklıysa hedefle değiştirilir.
  - Öğrenme: genel gruptan başlatılan profil `general_profile_id` olur. Ağa bağlı Özel gruptan o ağdayken başlatılan profil grubun `profile_id`'si olur.
  - Ayar: `network_auto_switch` (varsayılan açık).
- **UI:**
  - `GroupSectionBar`, grup sekmelerinin sol köşesindedir (`QTabWidget::setCornerWidget`): Genel | Özel (Ağ Bazlı) | Otomatik geçiş | şu anki ağ.
  - Sekme çubuğu tek bölümü listeler. `MainWindowInternal.h` içindeki `visibleGroupsTabOrder()` / `mw_network_section` `tabIndex2GroupId`/`groupId2TabIndex`'i süzer.
  - `show_group` diğer bölümden bir grup isterse o bölüme geçer.
  - Sekme sürükleme diğer bölümün gruplarının yerini korur.
  - Boş bölüme geçilince ilk grubu oluşturma diyaloğu açılır.
  - `NetworkScopeEditor`, grup diyaloğundaki "Kapsam" kutusudur: Genel/Özel, ağ listesi (Şu anki ağı ekle / Kaldır) ve "Çalıştır" profili.
- **Doğrulandı (yönetici olmadan, UIA + fare):**
  - Gerçek Wi-Fi algılandı: "SUPERBOX_Wi-Fi_0043 2", NLM GUID + ağ geçidi MAC (SSID 24H2'de okunamadı).
  - Benzetilmiş ağla: genel Socks çalışırken "TestNet"e geçince ağa özel Socks başladı, geri dönünce genel profile dönüldü. Elle durdurduktan sonra ağ değişimi hiçbir şey başlatmadı.
  - Bölüm düğmeleri sekmeleri doğru süzdü.
  - Boş Özel bölümünden "Ağım" grubu oluşturuldu ve gerçek ağa bağlandı (DB'de iki anahtar).
- **Doğrulanmadı:** gerçek bir ağ değişimi (başka Wi-Fi/Ethernet), kablolu ağda MAC/NLM anahtarları, yönetici modunda DPI ↔ ağa özel proxy geçişi.

### Blockcheck (F5 sonrası)
- **Motor:** `Engine::Blockcheck` (`src/engine/dpi/Blockcheck.cpp`). Native C++; zapret'in cygwin `blockcheck.cmd` script'ini kullanmaz. Worker thread'de çalışır, sinyaller ana thread'e gelir.
  1. Alan adları DoH ile çözülür: `https://1.1.1.1/dns-query` JSON API, proxy'siz QNAM. Olmazsa sistem DNS'i kullanılır. Böylece zehirli DNS sahte engel üretmez. Çözümleme, winws başlamadan yapılır.
  2. Taban ölçüm winws olmadan yapılır:
     - HTTP (80): `GET /`. Yanıt yoksa ya da başka bir siteye 3xx yönlendirmesi varsa engelli sayılır.
     - TLS 1.2 ve TLS 1.3 (443): `QSslSocket::connectToHostEncrypted(ip, 443, domain)` (SNI = alan adı), sertifika doğrulamalı. ISP'nin araya girdiği sayfa doğrulamadan geçemez.
     - QUIC test edilemez: Qt ve sistem curl HTTP/3 desteklemiyor.
  3. Yalnızca engelli çıkan hedefler (HTTP ve/veya TLS) için her strateji ayrı bir winws ile denenir: `--wf-tcp=<port> --filter-tcp=<port> <strateji>`, 500 ms grace. Puan = açılan engelli probe sayısı.
     - Hızlı: `Dpi::Blockcheck::QuickStrategies` (HTTP 7, TLS 17; basitten karmaşığa), tüm probe'ları açan ilk stratejide durur.
     - Derin: `DeepStrategies` (HTTP ~165, TLS ~199 kombinasyon), en yüksek puanlı ilk stratejiyi seçer.
  4. Sonuç `ComposeStrategy` ile oluşturulur: `--filter-tcp=80 <http>` / `--filter-tcp=443 <tls>` + standart QUIC fake bloğu, `wf_tcp`, `wf_udp=443`.
- **Dışlama ve güvenlik:**
  - Başlarken çalışan profil durdurulur (manual değil, `started_id` korunur) ve tarama bitince yeniden başlatılır.
  - Tarama süresince `ConnectionManager::setExclusiveTask` her `start()`'ı reddeder; NetworkSwitcher de buna dahildir.
  - Kill switch açıkken çalışmaz.
  - WinDivert açılamazsa (yönetici yok, sürücü engelli) tarama hemen anlaşılır bir hatayla biter.
  - Kendi `DpiSupervisor`'ı vardır (`killStrays=false`, `setQuiet(true)`: yüzlerce winws çalıştırması log'a yazılmaz).
- **UI:** `DialogBlockcheck` (`src/ui/dpi/`).
  - İçerik: siteler, Hızlı/Derin seçimi, protokoller, ilerleme, sonuç tablosu (Hedef/Strateji/Açılan), tarama günlüğü, özet.
  - Araçlar → Blockcheck… sonucu yeni DPI profili olarak kaydeder: ad "Blockcheck dd.MM HH:mm", genel bir gruba, mod "Tüm siteler".
  - DPI editöründeki Strateji → Blockcheck… sonucu açık profile uygular; profilin site listesini hedef olarak kullanır.
  - Pencere kapatılırsa tarama iptal edilir ve pencere temizlik bitince kapanır.
- **Doğrulandı:**
  - Yönetici olmadan: DoH çözümlemesi, taban ölçüm (discord.com HTTP/TLS1.2/TLS1.3 engelli, youtube.com açık), WinDivert hata mesajı, çalışan Socks'un durdurulup yeniden başlatılması.
  - Yönetici olarak (2026-10-23): Hızlı tarama ~10 sn'de `--dpi-desync=fake --dpi-desync-fooling=md5sig` buldu (TLS 2/2, HTTP 1/1). Sonuç DB'ye profil olarak kaydedildi.
  - Kaydedilen profil açılışta başlatıldığında winws çalıştı ve `discord.com` 200 döndü (öncesinde curl exit 35). `youtube.com` etkilenmedi; F-Dpi kapatılınca engel geri geldi.
- **UIA notları:**
  - Araçlar düğmesinin menüsü ok kısmına tıklanınca açılır.
  - Qt menü öğeleri UIA'ya görünmez; klavyeyle seçilir.
  - Blockcheck penceresi ana pencerenin UIA çocuğudur.

### F-Dpi bileşen tasarımı (sonraki fazlar)
- `dpi` tipi, `DpiSupervisor` ve dışlama: uygulandı (bkz. "DPI motoru (F2 sonrası)" ve "Motorlar ve karşılıklı dışlama").
- Genel / Özel ve `NetworkMonitor`: uygulandı (bkz. "Genel / Özel config (F4 sonrası)").
- Blockcheck: uygulandı (bkz. "Blockcheck (F5 sonrası)").
- **Otomatik DNS** (`include/dns/AutoDns.h`)
  - Adaylar: DoH/DoT sunucuları (Cloudflare, Google, Quad9, AdGuard, ...). Paralel gecikme testi yapılır.
  - Zehirlenme tespiti için bilinen engelli domain'lerin sistem DNS'i ve DoH cevapları karşılaştırılır.
  - Uygulama iki moda göre farklıdır:
    - Proxy modunda `RouteDns`/`remote_dns` üzerine yazılır.
    - DPI modunda (sing-box çalışmaz) Windows yerel DoH kullanılır: `netsh dns add encryption` + adaptör DNS'i. Durdurulunca orijinal ayarlar geri yüklenir; kalıcı yedek tutulur, çökme sonrası da geri yüklenir.
- **Yönetici:** Uygulandı (F1); bkz. "Yetki (F1 sonrası)".

## Geliştirme fazları
- [x] **F0 – Kurulum:** Throne klonu (`upstream`), `fetch_deps.ps1`, `build_windows.ps1`, ilk derleme, bu dosya.
- [x] **F1 – Markalama + Yönetici:**
  - `Branding.hpp`, `F-Dpi.exe`, `requireAdministrator`.
  - Admin opt-out gizlendi; update checker kapatıldı; About yenilendi.
  - `tr_TR` (tam çeviri) ve `ts_tool.js`.
  - `.gitattributes` ile `*.sh` LF'e sabitlendi.
  - Kalan iş: ikon tasarımı ve installer (F7'ye kaydırıldı).
- [x] **F2 – DPI motoru:**
  - `dpi` profil tipi, preset'ler, `Dpi::BuildLaunch` (split bypass listeleri dahil) ve `DpiSupervisor` (job object, kalıntı temizliği).
  - ConnectionManager'da karşılıklı dışlama (`startDpi`, crash-stop/`onCoreStarted` korumaları, `ActiveEngine`); kill switch ve sistem proxy'si kuralları.
  - Log paneline winws çıktısı; asgari `EditDpi` editörü; `tr_TR` çevirileri.
  - Doğrulandı (yönetici olmadan, UI Automation ile): profil yükleme/gösterim, liste dosyaları, winws argümanları, proxy→DPI geçişi (proxy durduruldu), hata mesajı, hatadan sonra proxy'nin yeniden başlaması, editör.
  - Yönetici olarak doğrulandı (2026-10-23, tek UAC onayıyla yükseltilmiş bir PowerShell betiği F-Dpi'yi başlatıp UI Automation ile sürdü; yükseltilmiş pencereye yükseltilmemiş süreçten tuş gönderilemez):
    - Bu ağda engelli `discord.com` (DoH ile bile curl exit 35) `general` preset + hostlist ile 200 döndü; `youtube.com` etkilenmedi.
    - DPI→proxy geçişinde winws durdu; proxy→DPI geçişinde yeniden başladı.
    - F-Dpi zorla kapatılınca winws de kapandı (job object) ve Discord yeniden engellendi; sonraki açılış temiz.
- [x] **F3 – DPI düzenleme UI'ı:**
  - `EditDpi` iki sekme: Strateji (preset, portlar, strateji, komut satırı önizlemesi) ve Site seçimi (mod + Siteler / Asla dokunma listeleri, hazır listeler, dosyadan içe aktarma, öğrenilen liste).
  - `build_windows.ps1` zapret dosyalarını yalnızca değiştiyse kopyalar (yüklü WinDivert sürücüsü `WinDivert64.sys`'i kilitler).
  - Doğrulandı (yönetici olmadan, UIA + fare): yeni profil → Yalnızca listedeki siteler + Discord + oyun hariç listesi → önizleme → kaydet; DB'de `mode=hostlist`, 11 site, 14 hariç. Otomatik modda winws argümanları WinDivert'e kadar kabul edildi, `autohostlist-1.txt` oluştu. Diyalog 1080p ekrana sığıyor.
  - Doğrulanmadı: otomatik modda winws'in gerçekten site öğrenmesi (yönetici + engelli site trafiği gerekir); "Dosyadan içe aktar" dosya seçicisi.
- [x] **F4 – Genel / Özel config:**
  - `network_scopes` tablosu + `NetworkScopesRepo`; ayarlar `network_auto_switch`, `general_profile_id`.
  - İkiye bölünmüş sekme alanı (`GroupSectionBar`); grup diyaloğunda kapsam (`NetworkScopeEditor`).
  - `Sys::NetworkMonitor` (Wi-Fi + Ethernet; NLM/ağ geçidi MAC/SSID) ve `Engine::NetworkSwitcher`.
  - Ayrıntı ve test sonuçları: "Genel / Özel config (F4 sonrası)".
- [x] **F5 – Blockcheck:** `Engine::Blockcheck`, `Dpi::Blockcheck` stratejileri, `DialogBlockcheck` (Araçlar menüsü + DPI editörü), `ConnectionManager::setExclusiveTask`, `DpiSupervisor` quiet/grace/killStrays. Ayrıntı: "Blockcheck (F5 sonrası)".
- [ ] **F6 – Otomatik DNS:**
  - Benchmark + zehirlenme tespiti.
  - Proxy modu (sing-box DNS) ve DPI modu (Windows DoH) uygulaması; geri yükleme/çökme kurtarma.
- [ ] **F7 – Sertleştirme ve paketleme:**
  - Çökme sonrası WinDivert/DNS temizliği.
  - Installer (`windows_installer.iss` yeniden markalanmış).
  - Smoke testleri, `RefusedForeignType`/yedekleme kenar durumları.

## Konvansiyonlar
- Kod stili Throne'unkini izler (`script/format_cpp.sh`, `.clang-format`); `tr()` ile çevrilebilir metin kullanılır.
- Yeni F-Dpi kodu yeni dosyalara yazılır (`*/dpi/*`, `sys/NetworkMonitor*`, `dns/*`). Throne dosyalarına yalnızca kanca/kayıt satırları eklenir; bu, upstream merge'lerini kolaylaştırır.
- Platforma özgü kod `src/sys/<os>/` altında, platformdan bağımsız arayüzlerin arkasında tutulur.
- Kaynak dosyalar `CMakeLists.txt` içinde açıkça listelenir; yeni dosya eklerken listeyi güncelle.
  - `<windows.h>` içeren Windows dosyaları `cmake/windows/windows.cmake` içindeki `PLATFORM_HEADLESS_SOURCES` listesine eklenir; bu liste MSVC unity batch'lerinin sonuna eklenir.
- MSVC unity build açık: aynı batch'teki dosyaların anonim namespace isimleri çakışabilir. Bu yüzden yardımcı fonksiyonlara dosyaya özgü önek verilir (ör. `dpiLaunch…`, `dpiSupervisor…`).
- Motor katmanı (`src/engine`, `configs`, `sys`, …) UI başlıklarını veya `qApp` kullanamaz; `cmake/EngineBoundary.cmake` her derlemede bunu denetler.
- `deps/`, `build/`, `deployment/` ve `res/dashboard/*` commit edilmez.
