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
# 3) Sürüm + installer -> deployment\F-Dpi-<sürüm>-windows-x64-setup.exe (önce: fetch_deps.ps1 -Only innosetup)
powershell -ExecutionPolicy Bypass -File script\build_windows.ps1 -Version 0.1.0 -Installer
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
| Inno Setup (taşınabilir kurulum: kaldırıcı ve kayıt girdisi yok) | jrsoftware/issrc | `deps/toolchain/innosetup` |

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
  - **Test betikleri bağımsız ve kendini temizleyen süreçler olmalı** (`Start-Process powershell -WindowStyle Hidden -File …`, yönetici için `-Verb RunAs`). Uygulama testi sırasında kullanıcının bağlantısı gidebilir ve Claude oturumu kesilebilir. Betik sonucu bir log dosyasına yazar, sonunda "== done" basar ve `finally` içinde süreçleri kapatıp proxy/DNS'i geri getirir; oturum log'u yoklayarak bekler.
  - Tuşlar (`SendKeys`) yalnızca F-Dpi penceresi öndeyken gönderilir (`GetForegroundWindow` + `GetWindowThreadProcessId`). Önde başka pencere varsa tuş oraya gider ve **ESC Claude'un çalışmasını keser**: ESC hiç kullanılmaz, diyaloglar kendi düğmeleriyle kapatılır.
  - Arka plandaki betik `SetForegroundWindow` ile pencereyi öne alamayabilir; gerçek bir fare tıklaması alır.
  - PowerShell'de `Kill` gibi yerleşik takma adlarla aynı adda fonksiyon yazılmaz: takma ad fonksiyondan önceliklidir. `Kill` = `Stop-Process` parametresiz kalıp gizli pencerede girdi bekler ve betik takılır.
  - PowerShell 5.1'de tek elemanlı sonuç dizi olarak dönmez: `(Get-Process winws).Count` ya da bir fonksiyonun döndürdüğü `@(...)` tek süreçte boş görünür. Sayı `return [int]@(...).Count` ile alınır.
  - Installer testini kullanıcı gibi yapın: installer yükseltilmemiş shell'den başlatılır (kendi UAC istemi), düğmeleri ayrı bir yönetici betiği tıklar. Yönetici betikten başlatılan installer'da "asıl kullanıcı" da yöneticidir ve `postinstall` hataları görünmez.
  - **Inno Setup sihirbazı** UI Automation'a düğme göstermez. Pencere `EnumWindows`/`EnumChildWindows` ile bulunur (`TSelectLanguageForm`, `TWizardForm`, `TUninstallProgressForm`, `#32770`) ve düğmelere `BM_CLICK` gönderilir. Inno 7 Türkçe düğme adları: Tamam, Sonraki, Kur, Bitti, Evet, Hayır; lisans radyosu "Anlaşmayı kabul ediyorum".

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
- Installer `PrivilegesRequired=admin` ile tüm kullanıcılar için `Program Files\F-Dpi` altına kurar (bkz. "Sertleştirme ve paketleme (F7 sonrası)").

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
- Throne ile yan yana kurulumda çakışabilecek Windows tanımlayıcıları F7'de F-Dpi'ye çevrildi:
  - Kurulum kayıt anahtarı `Software\F-Dpi` ve dosya ilişkilendirme ProgId'si `F-Dpi.Config` (`Branding::InstallRegistryKey/ConfigProgId`, `UrlScheme.cpp`).
  - Headless varlık eşlemesi `Global\FDpiHeadless` ve pencere sınıfı `FDpiHeadlessWindow` (`HostPresence_win.cpp`, `ServiceManager_win.cpp`). Böylece F-Dpi penceresi bir Throne headless'ını kendininki sanmaz. Servis modunun ProgramData klasörü `F-Dpi`.
  - Kill switch WFP sağlayıcısının ve kurallarının görünen adları "F-Dpi" oldu; sağlayıcı GUID'i zaten her oturumda rastgeledir. Çekirdeğin bağlantı satırı da "Connected to F-Dpi!".
  - İkonlar: `res/fdpi/` (`script/make_icons.ps1` üretir: kalkan + engeli kıran ok; Throne'un tepsi durum renkleri). `Throne.qrc` takma adları ve exe ikonu (`windows.cmake`) bunlara bakar. Throne'un `res/public` ve `res/Throne.ico` dosyaları upstream merge'leri için yerinde durur.
  - Kalan ortak nokta: `throne://` şeması iki uygulamada da kayıtlıdır; son açılan kazanır (Throne'un iki kopyasında da böyledir).
- İngilizce kaynak metinlerdeki "Throne" ifadelerine dokunulmaz, çünkü upstream çevirileri bozulur. Kullanıcıya görünen ad Türkçe çeviride "F-Dpi" olarak verilir.
- ⚠ Bu makinede `%APPDATA%\F-Dpi` ve `%LOCALAPPDATA%\F-Dpi` dizinleri bu repodan önce başka bir F-Dpi uygulaması tarafından oluşturulmuş (`profiles.json`, `settings.ini`, `fdpi.log`). Bizim uygulama portable modda bunlara dokunmaz. appdata modunda (`-appdata`, ya da exe dizini yazılamazsa) aynı kök dizini paylaşır, ama dosya adları çakışmaz. Installer'ın kaldırıcısı bu yüzden yalnızca `config\` alt klasörünü siler, `%APPDATA%\F-Dpi`'ye hiç dokunmaz.

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
  - Açılışta `EngineHost::init` içinde core başlamadan önce ağ eşzamanlı algılanır; `NetworkSwitcher::start()` core ile başlayacak profili döndürür.
    - **Açılışta bağlan** (`network_start_on_launch`, varsayılan açık; Tercihler → Genel sekmesinin en üstündeki "Başlangıç" kutusu; `DialogBasicSettings` kodla ekler): "son profili hatırla" kapalı olsa ve hiçbir şey hatırlanmasa bile bu ağın hedefi başlar. Elle durdurup çıkmak sonraki açılışı etkilemez.
    - Kapalıyken Throne davranışı: yalnızca hatırlanan profil (`remember_enable && remember_id`) başlar; otomatik geçiş açıksa bu ağın hedefiyle değiştirilir.
    - Doğrulandı (yönetici olmadan, 2026-10-24, `remember_enable=false`): TestNet'te ağa özel Socks, gerçek ağda genel Socks başladı; ayar kapalıyken hiçbir şey başlamadı.
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
- **Doğrulandı (yönetici, 2026-10-24, benzetilmiş ağ):** DPI çalışırken "TestNet"e geçince 8 sn'de DPI durdu, ağın Socks'u başladı ve Windows DNS'i geri yüklendi. Eve dönünce DPI yeniden başladı ve DNS yeniden uygulandı.
- **Doğrulanmadı:** gerçek bir ağ değişimi (başka Wi-Fi/Ethernet), kablolu ağda MAC/NLM anahtarları.

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
  - Yönetici olarak (2026-10-24), DPI editöründen: çalışan DPI profili tarama boyunca durdu ve sonra devam etti. Bulunan strateji (HTTP md5sig, TLS badseq) "Bu profilde kullan" ile editöre geldi, kaydedilince DB'ye yazıldı.
  - Derin Tarama (yönetici, 2026-10-24, discord.com + youtube.com): 364 stratejinin (HTTP 165, TLS 199) tamamı ~5 dakikada denendi. En iyi: HTTP `--methodeol` (1/1), TLS `--dpi-desync=fake --dpi-desync-fooling=md5sig` (2/2). Bu ağda arayüzdeki "30 dakika" uyarısı üst sınır kaldı.
- **UIA notları:**
  - Araçlar düğmesinin menüsü ok kısmına tıklanınca açılır.
  - Qt menü öğeleri UIA'ya görünmez; klavyeyle seçilir.
  - Blockcheck penceresi ana pencerenin UIA çocuğudur.

### Otomatik DNS (F6 sonrası)
- **Sağlayıcılar ve ayarlar:** `include/dns/AutoDns.h`.
  - `Dns::Providers()`: Cloudflare, Google, Quad9, AdGuard (filtresiz), OpenDNS, Mullvad. Hepsi filtresizdir. URL IP'ye sabitlenmiştir (`https://1.1.1.1/dns-query`), böylece çözümleyici gerekmez; Windows için ayrıca alan adlı DoH şablonu tutulur.
  - Ayarlar:
    - `auto_dns_mode`: `auto` = en hızlı, `off` ya da sağlayıcı id'si.
    - `auto_dns_proxy`, `auto_dns_system` (ikisi de varsayılan açık).
    - Benchmark'ın seçimi: `auto_dns_server`, `auto_dns_latency`, `auto_dns_network` (ağ anahtarı), `auto_dns_checked` (epoch sn, metin).
  - `Dns::ActiveProvider()`: elle seçilen sağlayıcı, yoksa benchmark seçimi, o da yoksa Cloudflare; kapalıysa `nullptr`.
  - `include/dns/DnsWire.h`: RFC 1035 sorgu ve yanıt (A/AAAA), DoH (RFC 8484 POST) ve düz UDP için.
- **Benchmark:** `Engine::DnsBenchmark` (`src/engine/dns/`).
  - Her sağlayıcı kendi thread'inde, proxy'siz QNAM ile ölçülür. İlk sorgu "bağlantı" süresidir (TLS dahil); sonraki popüler alan adları + test alan adları üzerinden medyan alınır.
  - Elenenler: yanıt vermeyenler (ağ DoH'u engelliyor olabilir), test alan adına bogon adres verenler (filtreliyor) ve sorguların yarısından azını yanıtlayanlar.
  - Zehirlenme testi, engelli siteler için üç yanıtı karşılaştırır: sistem DNS'i (adaptörün sunucusuna düz UDP; F-Dpi DNS'i değiştirdiyse yedekteki asıl sunucu), düz UDP ile 1.1.1.1 ve şifreli yanıtların birleşimi.
  - Kararlar: bogon adres ya da birden çok sitenin aynı adrese gitmesi (engel sayfası) → ZEHİRLİ; yanıt yok → engelli; şifreli yanıtla kesişim yok → farklı (CDN olabilir).
- **Proxy modu:** `generate.cpp` içinde `RouteDns::Current` sonrasına (iki yerde: normal ve test config'i) `Dns::ApplyToRouteDns` kancası eklendi.
  - Yalnızca **doğrudan DNS** değişir; uzak DNS zaten proxy'den geçer.
  - Dokunulmayan durumlar: yönlendirme profilinin kendi DNS'i varsa, `use_dns_object` açıksa, `auto_dns_proxy` kapalıysa.
  - Seçim değişirse çalışan proxy profili için `noteRestartNeeded(Settings)` çağrılır.
- **DPI modu (Windows DNS'i):** `Sys::SystemDns` (`src/sys/windows/SystemDns_win.cpp`, iphlpapi `Get/SetInterfaceDnsSettings` çalışma zamanında yüklenir, `DNS_INTERFACE_SETTINGS3`).
  - DPI başlayınca bağlı fiziksel adaptörlere (Wi-Fi/Ethernet, ağ geçidi olan) seçilen sunucunun IPv4/IPv6 adresleri yazılır: adaptör seviyesi `NameServer` + DoH özelliği (`DNS_DOH_SERVER_SETTINGS_ENABLE`, şablon, UDP'ye geri düşme yok). Ardından `DnsFlushResolverCache` çağrılır.
  - Ölçüldü: adaptör seviyesi `NameServer`, Windows 11'in ağ profili DNS'inden (`ProfileNameServer`, Ayarlar > Wi-Fi > DNS ataması) önceliklidir.
  - **Kullanıcının Windows'ta o ağ için ayarladığı DNS varsa (`ProfileNameServer` dolu) o adaptöre dokunulmaz**; log'a yazılır. `FDPI_SYSTEM_DNS_FORCE` ortam değişkeni (test) bu kuralı atlar.
  - Yedek `<config>/dpi/dns-backup.json` dosyasındadır: adaptör başına orijinal `NameServer` + DoH özellikleri (index/şablon/bayrak), yazılan değer (`set_v4/v6`) ve öncesindeki etkin sunucular (`effective`, zehirlenme testi için).
  - Geri yükleme yalnızca adaptörün şu anki sunucuları hâlâ F-Dpi'nin yazdığıysa yapılır; kullanıcı değiştirdiyse dokunulmaz.
  - Kancalar: `ConnectionDpi.cpp` (winws başladıktan sonra `dpiStarted`), `ConnectionManager::stop` stage2 (`m_dpi->stop()` sonrası `dpiStopped`). Uygulamadan çıkış da bu yoldan geçer.
  - Çökme sonrası: `EngineHost::init` içinde `AutoDnsService::recoverAtBoot()` (NetworkSwitcher ve profil devamından önce) yedeği geri yükler.
  - Ağ değişince DPI çalışıyorsa yeni bağlanan adaptöre de uygulanır.
- **`Engine::AutoDnsService`** (EngineHost, `Engine::host()->autoDns()`):
  - `auto` modunda açılıştan 8 sn sonra ve ağ değişince 5 sn sonra arka planda benchmark yapar (zehirlenme testi olmadan). Koşullar: seçim yoksa, başka ağda yapılmışsa ya da 7 günden eskiyse. TUN'lu proxy çalışırken ölçmez.
  - `adopt()` seçimi kaydeder; seçim değişince `settingsChanged()` çalışan DPI'nin Windows DNS'ini yeniler.
- **UI:** `DialogAutoDns` (`src/ui/dns/`, Araçlar → Otomatik DNS…).
  - Ayarlar: sunucu (Otomatik / sağlayıcılar / Kapalı) ve iki onay kutusu; "Şu an" satırı.
  - Test: siteler, sunucu tablosu (Bağlantı/Sorgu/Durum), zehirlenme tablosu (Sistem DNS'i / Düz DNS / Şifreli DNS) ve özet. Testin seçimi `adopt()` ile kaydedilir.
- **Doğrulandı (2026-10-24):**
  - Yönetici olmadan:
    - Açılışta arka planda Google seçildi (113 ms). Pencerede Cloudflare, Google, Quad9, AdGuard ve OpenDNS çalışıyor göründü (100–200 ms); Mullvad satırı tablonun görünür alanının dışındaydı.
    - Zehirlenme testi: bu makinenin sistem DNS'i zaten 1.1.1.1 (DoH) olduğu için "doğru".
    - Dışa aktarılan proxy config'inde `dns-direct` = `https 8.8.8.8` (öncesinde `local`).
  - Yönetici olarak, bağımsız betikle:
    - Elle DoH ayarlı Wi-Fi'ye dokunulmadı; DPI çalıştı (discord.com 200).
    - Zorla modda adaptör Google DoH'a geçti (DohFlags=2); `o-o.myaddr.l.google.com` yanıtı Google çözümleyicisine döndü.
    - Zorla kapatmadan sonra değişiklik kaldı; sonraki açılış geri yükledi.
    - DPI çalışırken Program → Çıkış geri yükledi.
    - Her durumda kayıt defteri başlangıçla aynı kaldı (`DohInterfaceSettings` altında boş anahtarlar kalabilir; zararsız).
- DPI çalışırken (benzetilmiş) ağ değişimi doğrulandı: proxy'ye geçişte DNS geri yüklendi, DPI'ye dönüşte yeniden uygulandı; yeni ağda arka plan benchmark'ı Cloudflare'i seçti (bkz. F4).
- **Doğrulanmadı:** DHCP DNS'li (zehirli ISS) bir ağda gerçek kullanım; Ethernet; gerçek ağ değişimi; QUIC/HTTP3 DNS'i (Chrome kendi DoH'unu kullanabilir).

### Sertleştirme ve paketleme (F7 sonrası)
- **WinDivert sürücüsü:** winws kapandıktan sonra da yüklü kalır ve `zapret\WinDivert64.sys` dosyasını kilitler; güncelleme ve kaldırma bu yüzden başarısız olur.
  - `Sys::StopDriverLoadedFrom(service, dir)` (`ChildProcess_win.cpp`) sürücüyü yalnızca ikili dosyası bu kurulumun `zapret` klasöründeyse ve oradan çalışan süreç yoksa durdurur. Böylece başka bir programın WinDivert'ine dokunulmaz. Durdurulan WinDivert servisi kendini siler.
  - `ExitController::shutdown` her düzgün çıkışta bunu çağırır (kill switch kapatıldıktan sonra).
  - Çökme sonrası: winws job object ile ölür; yüklü kalan boş sürücü trafiğe dokunmaz ve sonraki çıkışta ya da `-cleanup` ile durdurulur.
- **`F-Dpi.exe -cleanup`** (`Dpi::RunCleanup`, `src/dpi/DpiCleanup.cpp`; `main.cpp`'de veri dizini çözüldükten hemen sonra):
  - `config/dpi/dns-backup.json` varsa Windows DNS'ini geri yükler (`Dns::RestoreSystemDnsBackup`, AutoDnsService ile ortak) ve WinDivert'i durdurur.
  - Pencere açmaz, tek örnek kilidine dokunmaz, stderr'e yazar ve çıkar (~4 sn).
  - Installer'ın kaldırma ve güncelleme adımları kullanır: onlar F-Dpi'yi zorla kapatır, düzgün çıkış yolu çalışmaz.
- **Installer** (`script/windows_installer.iss`, Inno Setup 7, BOM'lu UTF-8):
  - Yeni AppId; `PrivilegesRequired=admin`, `{autopf}\F-Dpi`, yalnızca x64, Windows 10 1809+, Türkçe + İngilizce, lisans sayfası.
  - Dağıtım klasörünün `config\` (test verisi), `*.pdb` ve `updater.exe` dosyaları kuruluma girmez.
  - Güncelleme: `ssInstall` adımında süreçler (F-Dpi, ThroneCore, winws) kapatılır ve eski exe ile `-cleanup` çalıştırılır, ardından dosyalar kopyalanır.
  - `CloseApplications=no`: Restart Manager açıkken kapatılacak süreçleri `ssInstall`'dan önce listeliyordu. Onları StopFDpi kapattıktan sonra (ThroneCore F-Dpi'yle zaten ölür) olmayan süreçleri kapatamayıp sessiz güncellemeyi iptal ediyordu (exit 5).
  - StopFDpi her süreci ayrı `try` içinde kapatır; arada çıkmış bir süreç döngüyü kesmez.
  - Kaldırma: süreçler kapatılır, `-cleanup` çalıştırılır; ilişkilendirmeler (`throne://` yalnızca bu kopyayı açıyorsa), "F-Dpi AutoRun *" zamanlanmış görevleri ve WER dump anahtarları kaldırılır. İstenirse `config\` silinir.
  - Kurulum sonrası "F-Dpi'yi başlat" `runascurrentuser` ile kurulumun yönetici yetkisinde çalışır, ek UAC istemi açmaz. Inno, `postinstall` girdilerini varsayılan olarak kurulumu başlatan yükseltilmemiş kullanıcıyla çalıştırır; o zaman requireAdministrator exe CreateProcess 740 ("yükseltme gerekiyor") ile açılmaz. Kullanıcı bunu buldu; yönetici betikten başlatılan testlerde asıl kullanıcı zaten yönetici olduğu için görünmüyordu.
- **Yedek geri yükleme:** uzak istemciden (Remote API) gelen bir yedekteki DPI profili, Extra Core gibi reddedilir (`BackupArchive.cpp`, `BackupUntrustedRefusal`). winws yönetici olarak çalışır ve seçeneklerinin adını verdiği dosyaları okuyup sahte paket olarak gönderebilir. Pencereden yapılan yerel geri yükleme etkilenmez. Link, abonelik ve API içe aktarımı zaten `RefusedForeignType` ile reddediliyordu.
- **Sürüm:** `build_windows.ps1 -Version` (varsayılan 0.1.0) `INPUT_VERSION` olarak cmake'e geçer; pencere başlığı "F-Dpi 0.1.0", exe sürüm bilgisi 0.1.0.0.
- **Doğrulandı (2026-10-24):**
  - Yönetici olarak, kurulum testi (bağımsız betik):
    - Sessiz kurulum: doğru dosyalar, kayıt anahtarı, Başlat ve masaüstü kısayolları, sürüm 0.1.0.0.
    - Kurulu F-Dpi'de DPI ve zorla Otomatik DNS çalıştı; WinDivert `Program Files\F-Dpi\zapret`'tan yüklendi, discord.com 200 döndü.
    - DPI çalışırken sessiz kaldırma: süreçler kapandı, DNS birebir geri geldi, sürücü kaldırıldı; klasör, kayıt, kısayollar ve görevler temizlendi.
  - Yönetici olarak, geliştirme sürümünde DPI çalışırken Program → Çıkış: winws durdu, sürücü kaldırıldı (log: "WinDivert driver stopped").
  - Yönetici olmadan: `-cleanup` iş yokken 0 ile çıktı; pencere yeni ikonla "F-Dpi 0.1.0" başlığıyla açıldı.
- **Doğrulandı (son tur, 2026-10-24):**
  - Etkileşimli kurulum (Win32 mesajlarıyla sürülen sihirbaz): dil → lisans → konum → Kur → Bitti. Kurulum sonu F-Dpi'yi ek istem olmadan başlattı.
  - DPI çalışırken 0.1.0 → 0.1.1 sessiz yükseltme: F-Dpi ve winws kapandı, `-cleanup` 0 ile çıktı, DNS birebir geri geldi, sürücü kaldırıldı, profil korundu. 0.1.1 açılınca DPI devam etti, discord.com 200 döndü.
  - Etkileşimli kaldırma, DPI çalışırken ve verileri koruyarak ("Hayır"): DNS geri geldi, sürücü kaldırıldı, yalnızca `config\` kaldı; kayıt ve kısayollar silindi.
  - Upstream Throne (birleşme tabanından, ayrı worktree'de derlendi) ile yan yana: ikisi aynı anda çalıştı, her biri kendi çekirdeğiyle. İkinci açılışlar kendi kopyasına devretti; Throne kapatılınca F-Dpi ve çekirdeği çalışmayı sürdürdü.
- **Doğrulanmadı:** Throne'un da kurulu olduğu durumda dosya/bağlantı ilişkilendirmeleri (Throne portable çalıştırıldı).

### F-Dpi bileşen tasarımı (sonraki fazlar)
- `dpi` tipi, `DpiSupervisor` ve dışlama: uygulandı (bkz. "DPI motoru (F2 sonrası)" ve "Motorlar ve karşılıklı dışlama").
- Genel / Özel ve `NetworkMonitor`: uygulandı (bkz. "Genel / Özel config (F4 sonrası)").
- Blockcheck: uygulandı (bkz. "Blockcheck (F5 sonrası)").
- Otomatik DNS: uygulandı (bkz. "Otomatik DNS (F6 sonrası)").
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
  - Doğrulandı (2026-10-24):
    - Otomatik mod (yönetici): ilk üç discord.com isteği engellendi; winws siteyi `autohostlist-<id>.txt`'ye ekledi ve sonraki istekler 200 döndü.
    - "Dosyadan içe aktar" (Windows dosya seçicisi): URL'ler alan adına çevrildi, küçük harfe indirildi, tekrarlar atıldı.
- [x] **F4 – Genel / Özel config:**
  - `network_scopes` tablosu + `NetworkScopesRepo`; ayarlar `network_auto_switch`, `general_profile_id`.
  - İkiye bölünmüş sekme alanı (`GroupSectionBar`); grup diyaloğunda kapsam (`NetworkScopeEditor`).
  - `Sys::NetworkMonitor` (Wi-Fi + Ethernet; NLM/ağ geçidi MAC/SSID) ve `Engine::NetworkSwitcher`.
  - Ayrıntı ve test sonuçları: "Genel / Özel config (F4 sonrası)".
- [x] **F5 – Blockcheck:** `Engine::Blockcheck`, `Dpi::Blockcheck` stratejileri, `DialogBlockcheck` (Araçlar menüsü + DPI editörü), `ConnectionManager::setExclusiveTask`, `DpiSupervisor` quiet/grace/killStrays. Ayrıntı: "Blockcheck (F5 sonrası)".
- [x] **F6 – Otomatik DNS:** `Dns::Providers`/`DnsWire`, `Engine::DnsBenchmark` (hız + zehirlenme testi), `Engine::AutoDnsService` (arka plan seçimi, DPI sırasında Windows DoH, yedek/geri yükleme/çökme kurtarma), `Sys::SystemDns`, proxy doğrudan DNS kancası, `DialogAutoDns`. Ayrıntı: "Otomatik DNS (F6 sonrası)".
- [x] **F7 – Sertleştirme ve paketleme:** WinDivert sürücüsünün çıkışta durdurulması, `-cleanup` modu, F-Dpi installer'ı (Inno Setup, yönetici, TR/EN), ikonlar, yan yana kurulumda çakışan tanımlayıcılar, uzak yedekte DPI profilinin reddi, sürüm. Ayrıntı: "Sertleştirme ve paketleme (F7 sonrası)".

## Konvansiyonlar
- Kod stili Throne'unkini izler (`script/format_cpp.sh`, `.clang-format`); `tr()` ile çevrilebilir metin kullanılır.
- Yeni F-Dpi kodu yeni dosyalara yazılır (`*/dpi/*`, `sys/NetworkMonitor*`, `dns/*`). Throne dosyalarına yalnızca kanca/kayıt satırları eklenir; bu, upstream merge'lerini kolaylaştırır.
- Platforma özgü kod `src/sys/<os>/` altında, platformdan bağımsız arayüzlerin arkasında tutulur.
- Kaynak dosyalar `CMakeLists.txt` içinde açıkça listelenir; yeni dosya eklerken listeyi güncelle.
  - `<windows.h>` içeren Windows dosyaları `cmake/windows/windows.cmake` içindeki `PLATFORM_HEADLESS_SOURCES` listesine eklenir; bu liste MSVC unity batch'lerinin sonuna eklenir.
- MSVC unity build açık: aynı batch'teki dosyaların anonim namespace isimleri çakışabilir. Bu yüzden yardımcı fonksiyonlara dosyaya özgü önek verilir (ör. `dpiLaunch…`, `dpiSupervisor…`).
- Motor katmanı (`src/engine`, `configs`, `sys`, …) UI başlıklarını veya `qApp` kullanamaz; `cmake/EngineBoundary.cmake` her derlemede bunu denetler.
- `deps/`, `build/`, `deployment/` ve `res/dashboard/*` commit edilmez.
