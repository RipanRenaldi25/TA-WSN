# Dokumentasi  Sistem Smart Greenhouse IoT

Sistem *Smart Greenhouse* ini adalah solusi *Internet of Things* (IoT) terintegrasi penuh yang dirancang untuk memantau parameter pertanian dan mengotomatisasi sistem penyiraman (irigasi) tanaman. 

Sistem ini terbagi menjadi dua subsistem utama yang saling terhubung melalui jaringan radio **LoRa (Long Range)**:
1. **Sensor Node (End Node):** Perangkat bertenaga baterai di area lahan yang bertugas membaca berbagai sensor (Tanah, Udara, Cahaya), mengirimkan data, lalu masuk ke mode hemat daya (*Deep Sleep*).
2. **Gateway (Sink Node):** Perangkat pusat kontrol yang selalu aktif, bertugas menerima data dari seluruh *Sensor Node*, mengeksekusi logika irigasi (relai/pompa), menyimpan data *offline* (SD Card), dan mengirimkan data ke server *Cloud* (MQTT).

---

## 📡 BAGIAN 1: Standar Komunikasi Data (Shared Payload)

Kedua perangkat (Sensor Node dan Gateway) wajib memiliki pemahaman format data yang sama agar komunikasi radio via LoRa dapat terbaca dengan tepat. Data dibungkus dalam sebuah `struct` yang dipadatkan (*packed*) sehingga ukurannya konstan (22 byte) dan meminimalkan *overhead* transmisi.

```cpp
#pragma pack(push, 1) // Memaksa kompilator menghilangkan padding memori kosong
struct PayloadData {
  uint8_t nodeId;          // ID Pengirim (Contoh: Node 1 atau 2)
  uint8_t nodeTarget;      // ID Penerima (Gateway diset ke 255)
  int16_t soilMoisture;    // Kelembaban tanah (Nilai asli dikali 10)
  int16_t soilTemperature; // Suhu tanah (Nilai asli dikali 10)
  int16_t conductivity;    // EC (Electrical Conductivity) dalam us/cm
  int16_t soilPh;          // pH tanah (Nilai asli dikali 10)
  int16_t nitrogen;        // Kandungan Nitrogen (N) mg/kg
  int16_t phosporus;       // Kandungan Fosfor (P) mg/kg
  int16_t kalium;          // Kandungan Kalium (K) mg/kg
  int16_t airTemperature;  // Suhu udara dari DHT22 (Nilai asli dikali 10)
  int16_t airHumidity;     // Kelembaban udara dari DHT22 (Nilai asli dikali 10)
  int16_t lightIntensity;  // Intensitas cahaya dari LDR dalam satuan Lux
} __attribute__((packed));
#pragma pack(pop)
```
> **💡 Mengapa dikali 10 (`int16_t`)?**  
> Tipe data `float` (koma desimal) memakan memori 4 byte dan berat diproses saat transmisi radio. Dengan mengalikan desimal dengan 10 (contoh: `25.4` °C menjadi `254`), data dapat disimpan dalam tipe bilangan bulat 2-byte (`int16_t`). Gateway akan membaginya dengan 10 saat data diterima untuk mengembalikan nilai aslinya.

---

## 🛰️ BAGIAN 2: Sensor Node (Perangkat Pemantauan Lahan)

Sensor Node berjalan tanpa perulangan `loop()`. Seluruh eksekusi dilakukan secara sekuensial di dalam blok `setup()` yang kemudian diakhiri dengan ESP32 mematikan dirinya sendiri (*Deep Sleep*).

### A. Konfigurasi Perangkat Keras (Pinout)
| Komponen | Pin ESP32 | Penjelasan Teknis |
| :--- | :--- | :--- |
| **LoRa (SPI)** | `25` (SS), `26` (DIO0), `27` (RST) | MISO, MOSI, SCK menggunakan antarmuka SPI bawaan. |
| **Sensor NPK (RS485)** | `16` (RX2), `17` (TX2), `4` (RE/DE)| Berkomunikasi menggunakan protokol *Modbus RTU* via `Serial2`. Pin `4` (RE/DE) mengatur arah komunikasi. Tinggi (`HIGH`) untuk meminta data, Rendah (`LOW`) untuk menerima balasan. |
| **DHT22** | `32` | Sensor Suhu dan Kelembaban udara berbasis 1-Wire. |
| **LDR Analog** | `34` (Analog) | Menghasilkan nilai tegangan (`0-4095`), dikonversi menjadi satuan *Lux* melalui perhitungan resistansi matematis (Hukum Ohm). |
| **OLED (I2C)** | `21` (SDA), `22` (SCL) | Layar penampil lokal resolusi 128x64 dengan Alamat I2C `0x3C`. |

### B. Algoritma & Alur Kerja (*Workflow*)
1. **Inisialisasi Hardware:** Menyala dari mode tidur, ESP32 menginisialisasi jalur komunikasi (SPI, I2C, Serial2) dan menghidupkan layar OLED.
2. **Kalkulasi Waktu Tidur (*Collision Avoidance*):**  
   Fungsi `generateSleepTime()` menetapkan durasi tidur perangkat. Nilainya terdiri dari Waktu Dasar (15 Menit) + Acak (10 hingga 120 detik). Waktu acak ini sangat penting untuk mencegah fenomena *collision* (dua node mengirim sinyal radio LoRa di detik yang sama, yang menyebabkan paket rusak).
3. **Penyedotan Data Sensor:**
   * Sensor RS485 dipanggil dengan kode heksadesimal `[0x01, 0x03, 0x00, 0x00, 0x00, 0x07, 0x04, 0x08]`, meminta paket berisi parameter Moisture, Temperature, EC, pH, N, P, dan K secara serentak.
   * Nilai N, P, K mentah dimasukkan ke dalam fungsi kalibrasi regresi linier `calibratedData()` agar angkanya akurat sesuai sampel tanah referensi.
4. **Cek Jalur Udara (CSMA):**  
   Fungsi `checkChannel()` bekerja bagai radar telinga (*Listen Before Talk*). Node mendengarkan apakah ada *noise* atau paket dari Node lain di udara. Jika udara kosong (bebas transmisi), Node mengirimkan data `PayloadData` ke Gateway.
5. **Antarmuka Pengguna (UI):** Menampilkan 3 halaman data (Nutrisi, Tanah, Lingkungan) secara bergiliran pada layar OLED dengan jeda 5 detik per halaman.
6. **Penanganan Kondisi Kritis (*Wakeup Stream*):**  
   Sebelum Node masuk ke *Deep Sleep*, ada pengecekan terakhir. Jika `soilMoisture <= 20%` (tanah sangat kering), perangkat membatalkan niatnya untuk tidur panjang. Node akan bertahan menyala, membaca data, dan mem- *broadcast* paket LoRa setiap 5 detik selama 1 menit ke depan untuk memicu Gateway menyalakan pompa secepat mungkin.

---

## 🖲️ BAGIAN 3: Gateway / Sink Node (Pusat Kendali & Gateway Awan)

Gateway adalah inti logika sistem. Terhubung terus ke sumber listrik PLN, ESP32 Gateway beroperasi secara *Multitasking* (FreeRTOS) untuk menangani tugas berat tanpa tersendat.

### A. Konfigurasi Perangkat Keras (Pinout)
Gateway berurusan dengan komunikasi ke banyak modul sekaligus, sehingga sistem SPI dipisah menjadi dua bus agar SD Card dan LoRa tidak saling berebut lalu lintas data.
| Modul | Pin ESP32 | Penjelasan Teknis |
| :--- | :--- | :--- |
| **LoRa (VSPI Bus)** | `19` (MISO), `23` (MOSI), `18` (SCK), `25` (SS), `26` (DIO), `27` (RST) | Antarmuka independen penerima paket radio LoRa. |
| **SD Card (HSPI Bus)** | `5` (MISO), `13` (MOSI), `14` (SCK), `15` (CS) | Antarmuka *datalogger* lokal. |
| **Relay Valve** | `32` (Valve 1 untuk Node 1), `33` (Valve 2 untuk Node 2) | Kendali aktuator katup solenoida/pompa air (*Active LOW*). |
| **LCD & RTC DS3231** | `21` (SDA), `22` (SCL) | LCD (*Address `0x27`*) menampilkan status, RTC (*Real Time Clock*) memberi waktu pasti (Timestamp) untuk *database* atau log CSV. |

### B. Arsitektur FreeRTOS (Multitasking)
ESP32 memiliki dua otak (*Dual Core*). Sistem membagi tugas agar penerimaan paket sensor (yang berharga) tidak diblokir oleh proses *reconnect* WiFi yang lama.
1. **Core 1 (Fungsi `loop()` utama):**
   * Tugas eksklusif memeriksa paket LoRa yang masuk. Begitu `PayloadData` ditangkap dan tervalidasi sebagai miliknya (`nodeTarget == 255`), data diekstrak dan disuntikkan ke dalam lorong antrean `xQueueSend(dataQueue, ...)`.
   * Menghitung status kelembaban dari masing-masing node untuk mengambil keputusan apakah `Valve 1` atau `Valve 2` harus dinyalakan.
   * Menampilkan pembaruan data secara *real-time* ke layar LCD I2C.
2. **Core 0 (Task `networkManagementTask()`):**
   * Berjalan di latar belakang, tugas ini selalu memantau kesehatan koneksi WiFi dan MQTT.
   * Menerima kiriman data dari lorong antrean `xQueueReceive()`.
   * Jika internet *Online*, data di-*publish* ke MQTT. Jika internet *Offline*, data dibelokkan menuju fungsi penyimpanan memori lokal (SD Card).
> **🛡️ Semaphore Mutex (`displayMutex`):** Karena layar LCD ditulis oleh kedua *Core* secara bersamaan (Core 1 memperbarui sensor, Core 0 menampilkan status koneksi internet), sistem memakai kunci *Mutex*. *Core* mana pun yang ingin menulis layar harus "mengambil kunci", menulis, lalu "melepasnya". Ini mencegah teks di layar tumpang tindih berantakan.

### C. Logika Sistem Irigasi (*Watering Logic*)
Masing-masing Katup (Valve) memiliki mekanisme pengamanan canggih untuk mencegah insiden banjir akibat kerusakan sensor (seperti kabel sensor putus yang menyebabkan kelembaban selalu terbaca 0%).
1. **Mode Otomatis Dasar:** 
   * Menyiram air (Relay `LOW`) bila kelembaban tanah `<= 20%`.
   * Berhenti menyiram (Relay `HIGH`) bila kelembaban `>= 60%`.
2. **Safety Timeout (15 Detik):**
   * Gateway memiliki variabel pencatat waktu (`lastWatering` dan `lastValveActivate`). Bila sistem melihat katup sedang terbuka melampaui **15 detik**, sistem akan memutus arus secara sepihak meskipun tanah belum mencapai 60%.
3. **Kendali Mode Manual:**
   * Melalui jaringan internet awan (MQTT), operator bisa mengirim data melalui *dashboard* untuk memicu aktuator dari jarak jauh. Mengirimkan angka `0` akan menghidupkan katup dan memindahkan status sistem ke **Manual Mode**. (Fitur *timeout* 15 detik tetap berlaku dalam mode ini).

### D. Manajemen Data: Datalogger & MQTT (Offline-to-Online Sync)
Perangkat ini didesain kebal terhadap gangguan infrastruktur (Internet putus / listrik mati). Sistem format datanya adalah **CSV** (*Comma-Separated Values*), digabungkan dengan waktu dari RTC DS3231:
`"YYYY-MM-DD hh:mm:ss;NodeID;255;Moisture;Temperature;EC;pH;N;P;K;AirTemp;AirHumid;Lux"`

*   **Skenario Offline (Internet Putus):** Data akan dikirim ke SD Card, ditambahkan (di-*append*) di baris paling bawah pada file `/logFile.csv`.
*   **Skenario Online (Recovery Sync):** Begitu WiFi dan MQTT tersambung kembali, Gateway tidak lantas menembakkan ribuan data ke *broker* yang bisa menyebabkan alat "hang" (kehabisan RAM). Gateway akan membaca SD Card, mengirim **5 baris data (Batch Size)** setiap **5 menit**. Baris yang sukses dikirim akan dihapus dari SD Card. Siklus ini berulang hingga file log di SD Card bersih sepenuhnya.

### E. Daftar Topik Langganan & Publikasi (MQTT Pub/Sub)
Gateway berkomunikasi dengan *broker* `168.110.214.70` (Port `1883`) menggunakan parameter berikut:
*   **Publikasi Data Sensor (Upload):** `gh01/node/{nodeId}/parameter` (Berisi satu baris *string* CSV parameter lengkap).
*   **Publikasi Status Valve (Acknowledge):** `gh01/node/255/status/valve1` (Mengunggah string `ON` atau `OFF` ke *dashboard*).
*   **Subscribe Kendali Jarak Jauh (Remote):** `gh01/node/255/control/+` (Topik ini digunakan oleh aplikasi luar/dashboard untuk mengontrol Relay. Angka 0 = ON, 1 = OFF).
*   **Subscribe Status Cek (Ping):** `gh01/node/255/get/+` (Bila Gateway menerima notifikasi ini, Gateway dipaksa mengirim ulang status terkini katup ke *dashboard*).
