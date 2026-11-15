# Dokumentasi Notifikasi Telegram/Discord & Log Serial Monitor

## 1. Cara Membuat Bot Telegram (`@BotFather`)

Agar mikrokontroler dapat mengirimkan notifikasi langsung ke aplikasi Telegram di HP kamu:

1. Buka aplikasi Telegram, cari akun resmi **`@BotFather`** (centang biru), lalu kirim perintah:
   ```text
   /newbot
   ```
2. Masukkan nama tampilan bot (misal: `Kebun Pintar IoT`) dan username bot yang diakhiri kata `bot` (misal: `kebun_pintar_iot_bot`).
3. **Salin HTTP API Token** yang diberikan oleh `@BotFather` (contoh: `7123456789:AAHxyz1234567890abcdef`), lalu tempelkan ke `TELEGRAM_BOT_TOKEN` di dalam file `secrets.h`.
4. Klik tautan bot barumu dan tekan tombol **Start** (`/start`) minimal satu kali agar bot diizinkan mengirim pesan kepadamu.
5. Untuk mengetahui **`TELEGRAM_CHAT_ID`** akun pribadimu:
   - Cari bot **`@userinfobot`** di Telegram lalu klik **Start**, **ATAU**
   - Buka URL berikut di browser setelah mengirim pesan ke botmu:
     `https://api.telegram.org/bot<TOKEN_BOT_KAMU>/getUpdates`
   - Salin angka pada bagian `"chat":{"id": 123456789}` ke `TELEGRAM_CHAT_ID` di `secrets.h`.

---

## 2. Cara Membuat Discord Webhook (Opsional)

Jika kamu lebih suka menerima notifikasi di server Discord:

1. Buka **Server Settings > Integrations > Webhooks** pada channel Discord yang diinginkan.
2. Klik **New Webhook**, beri nama misalnya `Smart Plant Monitor`, lalu klik **Copy Webhook URL**.
3. Tempelkan URL tersebut ke `DISCORD_WEBHOOK_URL` di dalam file `secrets.h`.

---

## 3. Contoh Output Serial Monitor (115200 baud)

Berikut contoh log pada **Serial Monitor** ketika kelembapan tanah turun di bawah ambang kering (`< 35%`), memicu pompa menyala, mengirim notifikasi ke Telegram, hingga berhenti otomatis saat tanah kembali lembap (`>= 65%`):

```text
=======================================================
  IoT Lab - 03 Smart Plant Watering System             
  Target Board: ESP32
=======================================================
[WIFI] Menghubungkan ke SSID: Rumah_IoT ....
[WIFI] Terhubung!
[WIFI] Alamat IP Perangkat : 192.168.1.55
[CONFIG] Ambang Kering (ON): < 35% | Ambang Lembap (OFF): >= 65%
[CONFIG] Durasi Maks Pompa : 15 dtk | Cooldown Resapan: 30 dtk
[SOIL] ADC Rata-rata: 2450 | Kelembapan:  42% | Status: STANDBY (MONITOR)
[SOIL] ADC Rata-rata: 2680 | Kelembapan:  29% | Status: STANDBY (MONITOR)
--------------------------------------------------
[NOTIFY] [Smart Plant] Tanah Kering (29% < 35%). Memulai penyiraman otomatis.
--------------------------------------------------
[TELEGRAM] Notifikasi terkirim! (HTTP 200)
[SOIL] ADC Rata-rata: 2310 | Kelembapan:  49% | Status: MENYIRAM (PUMP ON)
[SOIL] ADC Rata-rata: 1980 | Kelembapan:  68% | Status: MENYIRAM (PUMP ON)
--------------------------------------------------
[NOTIFY] [Smart Plant] Penyiraman selesai (Target kelembapan >= 65% tercapai). Kelembapan: 29% -> 68%. Durasi: 4 detik. Cooldown aktif 30 detik.
--------------------------------------------------
[TELEGRAM] Notifikasi terkirim! (HTTP 200)
[SOIL] ADC Rata-rata: 1950 | Kelembapan:  69% | Status: COOLDOWN (RESAPAN)
[SYSTEM] Masa cooldown selesai. Sistem siap memantau penyiraman berikutnya.
[SOIL] ADC Rata-rata: 1950 | Kelembapan:  69% | Status: STANDBY (MONITOR)
```

---

## 4. Contoh Pesan Notifikasi yang Diterima di HP

### Saat Tanah Kering (Pompa Mulai Menyiram):
> **[Smart Plant]** Tanah Kering (29% < 35%). Memulai penyiraman otomatis.

### Saat Tanah Sudah Cukup Lembap (Pompa Berhenti):
> **[Smart Plant]** Penyiraman selesai (Target kelembapan >= 65% tercapai). Kelembapan: 29% -> 68%. Durasi: 4 detik. Cooldown aktif 30 detik.

### Saat Durasi Maksimum Tercapai (Pengaman Anti-Banjir):
> **[Smart Plant]** Penyiraman selesai (Batas waktu maksimum 15s tercapai). Kelembapan: 25% -> 52%. Durasi: 15 detik. Cooldown aktif 30 detik.
