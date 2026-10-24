<p align="center">
  <img src="res/fdpi/Running.png" width="96" alt="F-Dpi">
</p>

<h1 align="center">F-Dpi</h1>

<p align="center">
  <a href="README.md">English</a> · <b>Türkçe</b>
</p>

F-Dpi, internet sansürünü ve DPI (derin paket incelemesi) engellerini aşmak için bir Windows masaüstü uygulamasıdır. Tek
pencerede iki motoru bir araya getirir:

- **Proxy:** [sing-box](https://github.com/SagerNet/sing-box) ile, [Throne](https://github.com/throneproj/Throne)'un
  sunduğu her şey (VLESS, Trojan, Shadowsocks, Hysteria, WireGuard, abonelikler, yönlendirme, TUN modu, ...).
- **DPI Bypass:** [zapret](https://github.com/bol-van/zapret)'in `winws`'i ile. Sunucu gerektirmez: kendi trafiğinizi,
  sansür filtresinin tanıyamayacağı biçime sokar.

İkisi asla aynı anda çalışmaz: biri başlayınca diğeri durur.

F-Dpi, Throne üzerine kuruludur ve onun arayüzünü korur.

## Özellikler

- **DPI Bypass profilleri:** hazır stratejiler ya da kendi `winws` seçenekleriniz, profil düzenleyicide.
- **Blockcheck:** ağınızda işe yarayan stratejiyi bulur.
  - *Hızlı tarama* yaygın stratejileri dener ve tüm engelli siteleri açan ilk stratejide durur.
  - *Derin tarama* tüm kombinasyonları dener.
  - Sonuç yeni bir profil olarak kaydedilir ya da düzenlediğiniz profile uygulanır.
- **Seçici bypass:** bypass yalnızca engelli sitelere uygulanabilir (liste ya da otomatik öğrenme) ya da belirli siteler
  dışarıda tutulabilir. Discord ve YouTube listeleri ile dokunulmaması gereken oyun başlatıcıları listesi hazır gelir.
- **Genel ve ağa özel config'ler:** profil grupları ya *Genel*dir ya da bir Wi-Fi/Ethernet ağına bağlıdır.
  - F-Dpi, bağlandığınız ağın config'ini, yoksa genel config'i çalıştırır.
  - Ağ değişince profil kendiliğinden değişir.
- **Ağ yer tutucuları:** profile `%GATEWAY%`, `%LOCAL_IP%`, `%DHCP_DNS%`, `%INTERFACE%` ya da `%GATEWAY6%` yazın; F-Dpi
  profil başlarken o anki ağın değerini koyar.
  - Adresi her bağlantıda değişen telefon hotspot'ları içindir, örneğin `%GATEWAY%:10808` adresindeki bir SOCKS proxy'si.
  - Yer tutucular yalnızca bu bilgisayarın düzenleyicisinde kaydedilen profillerde çalışır, içe aktarılanlarda çalışmaz.
- **Otomatik DNS (DNS zehirlenmesine karşı):** F-Dpi şifreli DNS sağlayıcılarını (Cloudflare, Google, Quad9, AdGuard,
  OpenDNS, Mullvad) ölçer ve en hızlısını kullanır.
  - Ağınızın DNS'inin zehirli olup olmadığını test eder.
  - DPI Bypass çalışırken Windows'un kendisini DNS-over-HTTPS'e geçirir ve sonra ayarlarınızı geri yükler.
- **Açılışta bağlan:** F-Dpi açılınca bulunduğunuz ağın doğru config'ini başlatır.
- **Güvenle kaldırılır:** çıkış, güncelleme ve kaldırma; Windows DNS ayarlarını geri yükler ve WinDivert sürücüsünü
  kaldırır.

## Gereksinimler

- Windows 10 1809 ve üzeri, 64 bit. Windows 11 önerilir: DPI Bypass sırasında Otomatik DNS, Windows 11'in
  DNS-over-HTTPS desteğini kullanır.
- Yönetici yetkisi. F-Dpi her zaman yönetici olarak çalışır, çünkü WinDivert, TUN modu ve Windows DNS ayarları bunu
  gerektirir.

## Kurulum

[Releases](https://github.com/Drum3X/F-Dpi/releases) sayfasından `F-Dpi-<sürüm>-windows-x64-setup.exe` dosyasını indirip
çalıştırın. Program, tüm kullanıcılar için `Program Files\F-Dpi` altına kurulur. Kurulum sihirbazı Türkçe ve İngilizcedir.

## Hızlı başlangıç

1. **DPI Bypass:** *Araçlar → Blockcheck…*'i açın, engelli bir site girip hızlı tarama yapın. Sonucu profil olarak
   kaydedip başlatın.
2. **Proxy:** bir linkten ya da abonelikten profil ekleyin (Throne'daki gibi) ve başlatın. TUN modu ve sistem proxy'si
   varsayılan olarak açıktır.
3. **Ağa özel:** *Özel (Ağ Bazlı)*'ya geçin, bir grup oluşturun ve grubun *Kapsam* kutusunda *Şu anki ağı ekle*'ye basın.
   O ağ artık kendi config'ini kullanır.
4. **Otomatik DNS:** *Araçlar → Otomatik DNS…* seçilen sunucuyu gösterir ve ağınızı DNS zehirlenmesine karşı test eder.

## Kaynaktan derleme

Araç zinciri `deps/` klasörüne indirilir (yönetici yetkisi gerekmez). Tek sistem gereksinimi, C++ iş yüküyle Visual
Studio 2022 Build Tools'tur.

```powershell
# Araç zinciri ve çalışma zamanı dosyaları (Qt, Go, CMake, Ninja, protoc, sing-box, zapret, Inno Setup)
powershell -ExecutionPolicy Bypass -File script\fetch_deps.ps1
# Go çekirdeği + Qt arayüzü -> deployment\windows-amd64\
powershell -ExecutionPolicy Bypass -File script\build_windows.ps1
# Kurulum dosyasıyla -> deployment\F-Dpi-<sürüm>-windows-x64-setup.exe
powershell -ExecutionPolicy Bypass -File script\build_windows.ps1 -Version 0.1.0 -Installer
```

Geliştirici notları (mimari, kurallar, testlerin nasıl yapıldığı) [CLAUDE.md](CLAUDE.md) dosyasındadır.

## Teşekkür ve lisans

F-Dpi, [GNU GPL v3](LICENSE) lisanslı özgür bir yazılımdır. Şunların üzerine kuruludur:

- [Throne](https://github.com/throneproj/Throne) (GPL-3.0): temel aldığı uygulama.
- [sing-box](https://github.com/SagerNet/sing-box): proxy çekirdeği.
- [zapret](https://github.com/bol-van/zapret) ve [zapret-win-bundle](https://github.com/bol-van/zapret-win-bundle):
  `winws` ve listeleri.
- [WinDivert](https://github.com/basil00/WinDivert): Windows'ta paket yakalama.

## Sorumluluk reddi

F-Dpi, bulunduğunuz yerde engellenen bilgilere erişmek içindir. Ülkenizin yasalarına uymak sizin sorumluluğunuzdadır.
Yazılım hiçbir garanti olmadan sunulur.
