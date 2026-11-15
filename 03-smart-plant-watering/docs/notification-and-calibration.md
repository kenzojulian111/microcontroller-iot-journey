# Telegram Bot API, Discord Webhooks & UART Telemetry Logs — 03 Smart Plant Watering System

## 1. Provisioning a Telegram Bot via `@BotFather`

To enable encrypted HTTPS push notifications from the microcontroller directly to your Telegram client:

1. Open Telegram, search for the verified **`@BotFather`** account, and send the command:
   ```text
   /newbot
   ```
2. Provide a display name (e.g., `Kebun Pintar IoT`) and a unique username ending in `bot` (e.g., `kebun_pintar_iot_bot`).
3. Copy the **HTTP API Token** issued by `@BotFather` (e.g., `7123456789:AAHxyz1234567890abcdef`) and assign it to `TELEGRAM_BOT_TOKEN` in `secrets.h`.
4. Open a chat with your newly created bot and press **Start** (`/start`) at least once to authorize outbound messages.
5. Retrieve your numeric **`TELEGRAM_CHAT_ID`**:
   - Message **`@userinfobot`** on Telegram and press **Start**, **OR**
   - Send a test message to your bot and visit the following URL in a browser:
     `https://api.telegram.org/bot<YOUR_BOT_TOKEN>/getUpdates`
   - Copy the integer value inside `"chat":{"id": 123456789}` into `TELEGRAM_CHAT_ID` in `secrets.h`.

---

## 2. Provisioning a Discord Webhook (Optional)

To deliver alerts to a Discord server channel simultaneously or as an alternative to Telegram:

1. In your Discord server, navigate to **Server Settings > Integrations > Webhooks**.
2. Click **New Webhook**, name it `Smart Plant Monitor`, select the target channel, and click **Copy Webhook URL**.
3. Paste the URL into `DISCORD_WEBHOOK_URL` inside `secrets.h`.

---

## 3. UART Serial Monitor Output (`115200` baud)

The following UART log captures a complete closed-loop irrigation cycle: soil moisture drops below the lower hysteresis threshold (`< 35%`), energizing the pump and dispatching a Telegram notification, followed by automatic shutoff once moisture reaches the upper saturation threshold (`>= 65%`) and entry into the 30-second percolation cooldown period:

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

## 4. Push Notification Payload Examples

### a. Dry Threshold Breached (Irrigation Started)
> **[Smart Plant]** Tanah Kering (29% < 35%). Memulai penyiraman otomatis.

### b. Saturation Threshold Reached (Normal Shutoff)
> **[Smart Plant]** Penyiraman selesai (Target kelembapan >= 65% tercapai). Kelembapan: 29% -> 68%. Durasi: 4 detik. Cooldown aktif 30 detik.

### c. Maximum Run-Time Safety Cutoff Triggered (Anti-Overwatering Protection)
> **[Smart Plant]** Penyiraman selesai (Batas waktu maksimum 15s tercapai). Kelembapan: 25% -> 52%. Durasi: 15 detik. Cooldown aktif 30 detik.
