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
- **DPI motoru = `winws.exe`** (WinDivert, ayrı süreç).
  - Yeni bir `DpiSupervisor` tarafından yönetilir. Şablonu `CoreSupervisor` (`src/engine/supervisor/CoreSupervisor.cpp`): `CoreProcess : QProcess`, `DS_cores` thread'i, backoff/give-up durum makinesi, stdout→`MW_show_log`.
  - winws bir cygwin build'idir: `cygwin1.dll` aynı dizinde olmalıdır.
- **Dışlama noktası:** `ConnectionManager` (`src/engine/connection/ConnectionManager.cpp`).
  - Bugün tek aktif profil `m_running` + `mu_starting/mu_stopping` ile sağlanıyor; core da ikinci `Start`'ı reddediyor.
  - `RunningProfiles.hpp` yalnızca edit-lock amaçlıdır.
  - F-Dpi'de `EngineArbiter` (`None | Proxy | Dpi`) `ConnectionManager::start` / `EngineControl` seviyesinde devreye girer: önce diğer motoru durdurur, sonra hedefi başlatır.
  - Core süreci boşta çalışmaya devam edebilir (config yüklü değilken trafiğe dokunmaz).
  - DPI aktifken şu yollar profil başlatmamalıdır: `EngineHost::init` auto-resume (`started_id`/`remember_id`), `restartForGuard`, `applyPendingRestart`, `onCoreStarted`.
  - Kill switch açıksa winws için `KillSwitch::permitExtraCore(path)` benzeri bir WFP izni gerekir.

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
- C++ tarafında yok. Go core'daki `core/internal/netmon` yalnızca core çalışırken RPC ile sorgulanabiliyor.
- F-Dpi kendi `NetworkMonitor`'ünü yazar.
- `route_rules.wifi_ssid_json/wifi_bssid_json` (`RoutesRepo`) mevcut SSID eşleme modeli olarak yeniden kullanılabilir.

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

### F-Dpi bileşen tasarımı
- **`dpi` profil tipi** (`include/configs/outbounds/dpi.h`)
  - Strateji: `--filter-*` / `--dpi-desync*` argüman blokları (`--new` ile ayrılır) ve preset adı.
  - Filtre portları: `--wf-tcp`, `--wf-udp`.
  - Split bypass: `mode = all | hostlist | autohostlist`, `hostlist[]`, `exclude[]`, auto-hostlist dosyası.
  - Hepsi `outbound_json` içinde saklanır.
  - `Build()` sing-box tarafında kullanılmaz; winws argv'si `DpiConfig::BuildArgs()` ile üretilir.
- **`DpiSupervisor`** (`include/engine/dpi/`): winws sürecini yönetir.
  - Liste dosyalarını veri dizinine yazar.
  - Çıkışta WinDivert sürücüsünün bırakıldığını doğrular.
- **`EngineArbiter`**: proxy ve DPI karşılıklı dışlamasının tek doğruluk kaynağıdır (`ConnectionState` + aktif motor).
- **Genel / Özel**
  - `groups.scope` kolonu (`general` | `network`).
  - `network_bindings` tablosu: `network_key` → profile_id; anahtar Wi-Fi SSID(+BSSID opsiyonel) veya Ethernet NLM network GUID / gateway MAC.
  - Sekme alanı iki bölüm olur: Genel ve Özel (Ağ Bazlı).
- **`NetworkMonitor`** (`include/sys/NetworkMonitor.hpp` arayüz + `src/sys/windows/NetworkMonitor_win.cpp`)
  - Windows'ta NLM `INetworkListManager` olayları, `NotifyIpInterfaceChange`, WlanAPI (SSID) kullanılır.
  - Diğer platformlar için stub.
  - Ağ değişince eşleşen özel config yoksa genel config başlatılır.
- **Blockcheck** (`include/dpi/blockcheck/`): native C++; cygwin script'ine bağlı değildir.
  - Her strateji için winws'i başlatır, hedef domain'lere HTTP / TLS1.2 / TLS1.3 / QUIC denemesi yapar, sonuçları puanlar.
  - Hızlı Tarama ~20 küratörlü strateji dener; Derin Tarama desync × split-pos × fooling × ttl/autottl × repeats kombinasyonlarını üretir.
  - Çalışırken proxy motorunu durdurur (EngineArbiter üzerinden).
  - Bulunan sonuç tek tıkla bir `dpi` profiline uygulanır.
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
- [ ] **F2 – DPI motoru:**
  - `dpi` profil tipi ve `DpiSupervisor`.
  - `EngineArbiter` (karşılıklı dışlama, auto-resume yollarının korunması).
  - Log paneline winws çıktısı; deploy'a zapret.
- [ ] **F3 – DPI düzenleme UI'ı:**
  - `edit_dpi`: preset'ler, manuel argüman editörü, port filtreleri.
  - Split Bypass: mod seçimi + hostlist / exclude / auto-hostlist editörleri.
  - `DialogEditProfile` entegrasyonu.
- [ ] **F4 – Genel / Özel config:**
  - `groups.scope` + `network_bindings` şeması ve migrasyonu.
  - İkiye bölünmüş sekme alanı; grup diyaloğunda kapsam seçimi.
  - `NetworkMonitor` (Wi-Fi + Ethernet) ve otomatik geçiş.
- [ ] **F5 – Blockcheck:**
  - Test motoru (strateji × protokol).
  - Hızlı/Derin tarama, ilerleme UI'ı, iptal.
  - Sonucu profile uygulama.
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
- `deps/`, `build/`, `deployment/` ve `res/dashboard/*` commit edilmez.
