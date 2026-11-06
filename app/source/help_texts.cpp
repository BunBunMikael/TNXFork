#include "help_texts.h"

std::map<std::string, const char*> HELP_TEXTS;

void initHelpTexts() {
    HELP_TEXTS["TR"] = u8R"(### 1. Aşama: Kritik Ön Hazırlık (Zorunlu)
Uygulamayı kullanmaya başlamadan önce Switch'inizin çökmesini (crash) önlemek için UltraHand bellek ayarını değiştirmeniz şarttır.

UltraHand menüsünü açın.
+ tuşuna basarak ayarlara, ardından System bölümüne girin.
Overlay Memory (Arayüz Belleği) değerini 4MB'den 8MB'a yükseltin.

### 2. Aşama: Gerekli API'lerin Alınması
TranslateNX'in en iyi performansla çalışması için iki farklı sisteme ihtiyacı vardır:
OCR (Optik Karakter Tanıma): Ekrandaki yazıları tarayıp düzenlenebilir ve kopyalanabilir metne dönüştürür.
Çeviri Sistemi: Taranan metni anında istediğiniz dile çevirir.
(Tavsiyemiz: En iyi, en hızlı ve ücretsiz deneyim için OCR olarak Google Cloud Vision, çeviri için ise DeepL kullanmanızdır. Diğer alternatifleri de aşağıda bulabilirsiniz.)

## OCR API Seçenekleri

## 1. Google Cloud Vision (Aylık 1000 Kullanım) — ⭐ ÖNERİLEN
Google Cloud Vision adresine gidin ve "Try Vision AI Free" butonuna tıklayıp gerekli kayıt işlemlerini tamamlayın.
Açılan Cloud Vision API sayfasında "Enable" (Etkinleştir) butonuna basın.
Sol menüden Credentials (Kimlik Bilgileri) sekmesine girin. Üst kısımdan Create Credentials > API Key yolunu izleyin.
API'nize bir isim verin. Select API restrictions kısmından Cloud Vision API'yi seçin ve OK'a, ardından Create'e basın. API anahtarınız saniyeler içinde ekranda belirecektir.

## 2. OCR.space (Günlük 500 Kullanım) — Alternatif
OCR.space Free Key adresinden kayıt olun.
E-posta adresinize gelen onay bağlantısına tıklayın. Onayladıktan saniyeler sonra API anahtarınızın bulunduğu ikinci bir e-posta alacaksınız.

## Çeviri API Seçenekleri

## 1. DeepL (Tek Seferlik 1.000.000 Karakter) — ⭐ ÖNERİLEN
Not: 1 milyon karakterlik kota genellikle sizi çok uzun süre idare edecektir. Kota dolduğunda yeni bir ücretsiz hesap açabilir veya ücretli plana geçebilirsiniz.
DeepL sitesine gidip kayıt olun. Sağ üstteki profil simgenize, ardından Hesap butonuna tıklayın.
Sol alt köşedeki API Plans menüsüne girin ve ücretsiz plana abone olun (Kayıt için bazı bilgiler isteyebilir ancak ücret kesmez).
Açılan ekranda API Keys & Limits sekmesine tıklayın ve Create Key butonuna basın.
API'nize bir isim verin, "All access" seçeneğini işaretleyin ve Create Key'e tıklayın.

## 2. Google Cloud Translate (Aylık 500.000 Karakter) — Alternatif
Not: Kayıt sırasında kart bilgisi isteyebilir ve küçük bir provizyon ücreti kesip iade edebilir. Aylık kotayı aşmadığınız sürece tamamen ücretsizdir. Eğer bunu kullanacaksanız kotanızı ara ara kontrol etmenizi öneririz.
Google Cloud Translate adresine gidin ve "Try Translation API" butonuna tıklayıp kayıt işlemlerini tamamlayın.
Cloud Translation API sayfasında "Enable" butonuna basın.
Credentials sekmesine girip Create Credentials > API Key yolunu izleyin.
API'nize isim verin, kısıtlamalar bölümünden Cloud Translation API'yi seçip Create butonuna basın. Anahtarınız ekranda belirecektir.

## 3. MyMemory (Günlük 5.000 Karakter) — Kurtarıcı
Bu sistem TranslateNX içinde hazır kurulu gelir. API anahtarı almanıza gerek yoktur.
Uygulama içindeki Çeviri API ayarlarından direkt seçerek zor anlarda pratik bir şekilde kullanabilirsiniz.

### 3. Aşama: API Anahtarları Nereye Girilecek?
Aldığınız API anahtarlarını uygulamaya tanıtmak için aşağıdaki iki yöntemden birini tercih edebilirsiniz:
Yöntem 1 (Switch Üzerinden): Hbmenu üzerinden TranslateNX uygulamasına girerek API anahtarlarınızı doğrudan uygulama arayüzünden ilgili yerlere yazabilirsiniz.
Yöntem 2 (Bilgisayar Üzerinden - Pratik Yol): Switch'inizi bilgisayarınıza bağlayın. SdCard/config/translate yolundaki config.ini dosyasını bilgisayarınızın masaüstüne alın. Dosyayı bir metin belgesi olarak açıp API anahtarlarını gerekli yerlere yapıştırın, kaydedin ve dosyayı tekrar Switch'teki aynı klasörün içine atın.

### 4. Aşama: Uygulamanın Kullanımı
Önemli Not: Cihazınızda UltraHand ve SysModule'ün yüklü olduğundan emin olun.
UltraHand'i Açın: Parmağınızı ekranın sol kenarından sağa doğru kaydırarak veya L + R + D-Pad Aşağı + Sağ Analog (R-Stick) kombinasyonuna basarak UltraHand arayüzünü çağırın.
TranslateNX'i Başlatın: Listeden Translate seçeneğinin üzerine gelin ve uygulamayı açın. (İpucu: Üzerindeyken Y tuşuna basarak hızlı erişim için kısayol atayabilirsiniz).
Ayarları Yapılandırın: Ayarlar bölümüne girin. Aldığınız OCR ve Çeviri API'lerini seçin.
Dilleri Belirleyin:
Kaynak Dil: Oynadığınız oyunun orijinal dili.
Hedef Dil: Çevirinin yapılmasını istediğiniz kendi diliniz.
Çeviriye Başlayın: Oyunda çevirmek istediğiniz bir metin ekrandayken TranslateNX arayüzünü açın ve "Çeviriye Başla" butonuna basın.
Not: İşlem ilk seferinde biraz yavaş olabilir, çeviri hızı kullandığınız API'ye ve internet bağlantınıza göre değişiklik gösterecektir.
Sonucu Görüntüleyin: Kısa bir süre sonra ekranda tespit edilen yazıların listesi karşınıza çıkacaktır. Çevirisini detaylı görmek istediğiniz metnin üzerine tıklamanız yeterlidir.)";
    HELP_TEXTS["EN"] = u8R"(### Stage 1: Critical Preparation (Mandatory)
Before you start using the app, it is essential to change the UltraHand memory setting to prevent your Switch from crashing.

Open the UltraHand menu.
Press + to enter settings, then System.
Increase Overlay Memory from 4MB to 8MB.

### Phase 2: Obtaining Required APIs
TranslateNX needs two different systems to run at best performance:
OCR (Optical Character Recognition): Scans the text on the screen and converts it into editable and copyable text.
Translation System: Instantly translates the scanned text into the language you want.
(Our advice: For the best, fastest and free experience, use Google Cloud Vision for OCR and DeepL for translation. You can find other alternatives below.)

## OCR API Options

## 1. Google Cloud Vision (1000 Monthly Uses) — STAR RECOMMENDED
Go to Google Cloud Vision and click on the "Try Vision AI Free" button and complete the necessary registration procedures.
On the Cloud Vision API page that opens, click the "Enable" button.
Enter the Credentials tab from the left menu. From the top, follow the path Create Credentials > API Key.
Give your API a name. Select Cloud Vision API from the Select API restrictions section and press OK, then Create. Your API key will appear on the screen within seconds.

## 2. OCR.space (500 Uses per Day) — Alternative
Sign up at OCR.space Free Key.
Click on the confirmation link sent to your e-mail address. Seconds after confirming, you will receive a second email with your API key.

## Translation API Options

## 1. DeepL (1,000,000 One-Time Characters) — STAR RECOMMENDED
Note: A 1 million character quota will usually last you a very long time. When the quota is reached, you can open a new free account or switch to a paid plan.
Go to the DeepL website and register. Click on your profile icon in the top right, then click the Account button.
Enter the API Plans menu in the lower left corner and subscribe to the free plan (It may ask for some information for registration, but it does not charge a fee).
On the screen that opens, click on the API Keys & Limits tab and press the Create Key button.
Give your API a name, check "All access" and click Create Key.

## 2. Google Cloud Translate (500,000 Characters per Month) — Alternative
Note: It may ask for card information during registration and charge a small provisioning fee for a refund. It is completely free as long as you do not exceed the monthly quota. If you are going to use this, we recommend that you check your quota from time to time.
Go to Google Cloud Translate and click on the "Try Translation API" button and complete the registration process.
Click the "Enable" button on the Cloud Translation API page.
Go to the Credentials tab and follow the path Create Credentials > API Key.
Name your API, select Cloud Translation API from the restrictions section and press the Create button. Your key will appear on the screen.

## 3. MyMemory (5,000 Characters per Day) — Savior
This system comes ready-installed in TranslateNX. You do not need to obtain an API key.
You can use it practically in difficult moments by selecting it directly from the Translation API settings within the application.

### Phase 3: Where to Enter API Keys?
You can choose one of the following two methods to introduce the API keys you receive to the application:
Method 1 (Via Switch): You can enter the TranslateNX application via Hbmenu and write your API keys to the relevant places directly from the application interface.
Method 2 (Through Computer - Practical Way): Connect your Switch to your computer. Import the config.ini file in the SdCard/config/translate path to your computer's desktop. Open the file as a text document, paste the API keys where necessary, save and put the file back into the same folder on the Switch.

### Phase 4: Use of the Application
Important Note: Make sure UltraHand and SysModule are installed on your device.
Open UltraHand: Call up the UltraHand interface by sliding your finger from the left edge of the screen to the right or by pressing the combination L + R + D-Pad Down + Right Analog (R-Stick).
Launch TranslateNX: Point to Translate from the list and open the application. (Tip: You can assign a shortcut for quick access by pressing Y while on it).
Configure Settings: Enter the Settings section. Select the OCR and Translation APIs you received.
Specify Languages:
Source Language: The original language of the game you are playing.
Target Language: Your own language in which you want the translation to be made.
Start Translating: While the text you want to translate in the game is on the screen, open the TranslateNX interface and press the "Start Translation" button.
Note: The process may be a little slow the first time, translation speed will vary depending on the API you use and your internet connection.
View the Result: After a short while, the list of detected texts will appear on the screen. Just click on the text you want to see its translation in detail.)";
}
