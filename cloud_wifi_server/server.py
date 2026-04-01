from __future__ import annotations

from datetime import datetime
from typing import Any

from flask import Flask, Response, jsonify, request

app = Flask(__name__)


def now_iso() -> str:
    return datetime.now().isoformat(timespec="seconds")


def default_command() -> dict[str, Any]:
    return {
        "mode": "stop",
        "speed": 170,
        "turn": 140,
        "buzzer": False,
        "updated_at": now_iso(),
    }


def default_telemetry(robot_id: str) -> dict[str, Any]:
    return {
        "robot_id": robot_id,
        "state": "offline",
        "preheating": True,
        "alarm": False,
        "methane_fault": False,
        "co_fault": False,
        "ch4_ppm": 0.0,
        "co_ppm": 0.0,
        "temperature_c": 0.0,
        "humidity_rh": 0.0,
        "distance_cm": -1.0,
        "uptime_s": 0,
        "wifi_rssi": 0,
        "updated_at": now_iso(),
    }


ROBOTS: dict[str, dict[str, Any]] = {
    "gas-car-01": {
        "telemetry": default_telemetry("gas-car-01"),
        "command": default_command(),
    }
}


def get_robot(robot_id: str) -> dict[str, Any]:
    if robot_id not in ROBOTS:
        ROBOTS[robot_id] = {
            "telemetry": default_telemetry(robot_id),
            "command": default_command(),
        }
    return ROBOTS[robot_id]


def clamp_int(value: Any, minimum: int, maximum: int, fallback: int) -> int:
    try:
        return max(minimum, min(maximum, int(value)))
    except (TypeError, ValueError):
        return fallback


HTML_PAGE = """<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>气体监测报警小车控制台</title>
  <style>
    :root {
      --bg: #f2f5f8;
      --panel: #ffffff;
      --line: #dce3ea;
      --text: #16324f;
      --muted: #5f7388;
      --accent: #0c7c59;
      --warn: #d1495b;
      --shadow: 0 12px 30px rgba(18, 48, 76, 0.08);
    }
    * { box-sizing: border-box; }
    body {
      margin: 0;
      font-family: "Microsoft YaHei", "Segoe UI", sans-serif;
      background: linear-gradient(180deg, #eef4f8 0%, #f8fafc 100%);
      color: var(--text);
    }
    .wrap {
      max-width: 1080px;
      margin: 0 auto;
      padding: 24px;
    }
    .hero, .panel {
      background: var(--panel);
      border: 1px solid var(--line);
      border-radius: 18px;
      box-shadow: var(--shadow);
    }
    .hero {
      padding: 20px 24px;
      margin-bottom: 20px;
    }
    .hero h1 {
      margin: 0 0 8px;
      font-size: 28px;
    }
    .hero p {
      margin: 0;
      color: var(--muted);
      line-height: 1.6;
    }
    .grid {
      display: grid;
      grid-template-columns: 1.2fr 1fr;
      gap: 20px;
    }
    .panel {
      padding: 20px;
    }
    .toolbar {
      display: grid;
      grid-template-columns: 1fr 1fr;
      gap: 12px;
      margin-bottom: 18px;
    }
    label {
      font-size: 13px;
      color: var(--muted);
      display: block;
      margin-bottom: 6px;
    }
    input[type="text"], input[type="range"] {
      width: 100%;
    }
    input[type="text"] {
      border: 1px solid var(--line);
      border-radius: 12px;
      padding: 10px 12px;
      font-size: 15px;
    }
    .stats {
      display: grid;
      grid-template-columns: repeat(2, 1fr);
      gap: 12px;
    }
    .stat {
      border: 1px solid var(--line);
      border-radius: 14px;
      padding: 14px;
      background: #fbfcfd;
    }
    .stat .k {
      color: var(--muted);
      font-size: 12px;
      margin-bottom: 6px;
    }
    .stat .v {
      font-size: 24px;
      font-weight: 700;
    }
    .pad {
      display: grid;
      grid-template-columns: repeat(3, 1fr);
      gap: 10px;
      margin-top: 12px;
    }
    button {
      border: none;
      border-radius: 14px;
      padding: 14px 12px;
      font-size: 15px;
      font-weight: 700;
      color: white;
      background: #276fbf;
      cursor: pointer;
    }
    button.secondary { background: #6c7a89; }
    button.warn { background: var(--warn); }
    button.good { background: var(--accent); }
    .status {
      margin-top: 16px;
      padding: 12px 14px;
      border-radius: 12px;
      background: #edf6f2;
      color: #21563f;
      font-weight: 600;
    }
    .status.alarm {
      background: #ffedf0;
      color: #9f2030;
    }
    .tiny {
      font-size: 12px;
      color: var(--muted);
      margin-top: 10px;
      line-height: 1.5;
    }
    @media (max-width: 840px) {
      .grid { grid-template-columns: 1fr; }
      .stats { grid-template-columns: 1fr 1fr; }
    }
  </style>
</head>
<body>
  <div class="wrap">
    <div class="hero">
      <h1>气体监测报警小车控制台</h1>
      <p>浏览器下发控制指令，小车轮询服务器获取命令并上传甲烷、CO、温湿度和避障数据。这个页面默认用于教学/答辩演示。</p>
    </div>

    <div class="grid">
      <section class="panel">
        <div class="toolbar">
          <div>
            <label for="robotId">机器人 ID</label>
            <input id="robotId" type="text" value="gas-car-01">
          </div>
          <div>
            <label for="speed">速度 PWM：<span id="speedValue">170</span></label>
            <input id="speed" type="range" min="80" max="255" value="170">
          </div>
        </div>

        <div class="stats">
          <div class="stat"><div class="k">甲烷浓度</div><div class="v" id="ch4">0 ppm</div></div>
          <div class="stat"><div class="k">CO 浓度</div><div class="v" id="co">0 ppm</div></div>
          <div class="stat"><div class="k">温度</div><div class="v" id="temp">0 ℃</div></div>
          <div class="stat"><div class="k">湿度</div><div class="v" id="hum">0 %RH</div></div>
          <div class="stat"><div class="k">前方距离</div><div class="v" id="dist">- cm</div></div>
          <div class="stat"><div class="k">当前状态</div><div class="v" id="state">offline</div></div>
        </div>

        <div class="status" id="statusBox">等待小车上线...</div>
        <div class="tiny" id="metaLine">最后更新：-</div>
      </section>

      <section class="panel">
        <label>远程控制</label>
        <div class="pad">
          <button class="secondary" onclick="sendMode('left')">左转</button>
          <button onclick="sendMode('forward')">前进</button>
          <button class="secondary" onclick="sendMode('right')">右转</button>
          <button class="secondary" onclick="sendMode('backward')">后退</button>
          <button class="warn" onclick="sendMode('stop')">停止</button>
          <button class="good" onclick="sendMode('auto')">自动巡航</button>
        </div>

        <div class="pad" style="margin-top:10px;">
          <button onclick="setBuzzer(true)">鸣笛</button>
          <button class="secondary" onclick="setBuzzer(false)">关蜂鸣器</button>
          <button class="good" onclick="refreshTelemetry()">刷新数据</button>
        </div>

        <div class="tiny">
          云端版建议用于有 Wi-Fi 的实验环境。真实工程部署时，更适合改为 MQTT/WebSocket + 权限控制 + 持久化数据库。
        </div>
      </section>
    </div>
  </div>

  <script>
    const robotInput = document.getElementById("robotId");
    const speedInput = document.getElementById("speed");
    const speedValue = document.getElementById("speedValue");

    speedInput.addEventListener("input", () => {
      speedValue.textContent = speedInput.value;
    });

    async function sendCommand(payload) {
      const robotId = robotInput.value.trim() || "gas-car-01";
      payload.speed = Number(speedInput.value);
      payload.turn = 140;
      const response = await fetch(`/api/command/${robotId}`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(payload)
      });
      if (!response.ok) {
        alert("指令发送失败");
      }
    }

    function sendMode(mode) {
      sendCommand({ mode });
    }

    function setBuzzer(enabled) {
      sendCommand({ buzzer: enabled });
    }

    async function refreshTelemetry() {
      const robotId = robotInput.value.trim() || "gas-car-01";
      const response = await fetch(`/api/telemetry/${robotId}`);
      if (!response.ok) {
        return;
      }
      const data = await response.json();
      document.getElementById("ch4").textContent = `${Number(data.ch4_ppm || 0).toFixed(0)} ppm`;
      document.getElementById("co").textContent = `${Number(data.co_ppm || 0).toFixed(1)} ppm`;
      document.getElementById("temp").textContent = `${Number(data.temperature_c || 0).toFixed(1)} ℃`;
      document.getElementById("hum").textContent = `${Number(data.humidity_rh || 0).toFixed(1)} %RH`;
      document.getElementById("dist").textContent = data.distance_cm >= 0 ? `${Number(data.distance_cm).toFixed(0)} cm` : "- cm";
      document.getElementById("state").textContent = data.state || "unknown";

      const statusBox = document.getElementById("statusBox");
      const preheating = Boolean(data.preheating);
      const alarm = Boolean(data.alarm);
      if (alarm) {
        statusBox.className = "status alarm";
        statusBox.textContent = "报警中：已触发停机/蜂鸣保护";
      } else if (preheating) {
        statusBox.className = "status";
        statusBox.textContent = "传感器预热中：暂不执行移动命令";
      } else {
        statusBox.className = "status";
        statusBox.textContent = "运行正常：可远程查看和控制";
      }

      document.getElementById("metaLine").textContent =
        `最后更新：${data.updated_at || "-"}  |  Wi-Fi RSSI：${data.wifi_rssi ?? "-"} dBm  |  运行时长：${data.uptime_s ?? 0}s`;
    }

    setInterval(refreshTelemetry, 1000);
    refreshTelemetry();
  </script>
</body>
</html>
"""


@app.get("/")
def index() -> Response:
    return Response(HTML_PAGE, mimetype="text/html")


@app.get("/api/telemetry/<robot_id>")
def get_telemetry(robot_id: str) -> Response:
    robot = get_robot(robot_id)
    return jsonify(robot["telemetry"])


@app.post("/api/telemetry")
def post_telemetry() -> Response:
    payload = request.get_json(force=True, silent=False) or {}
    robot_id = str(payload.get("robot_id", "gas-car-01"))
    robot = get_robot(robot_id)
    telemetry = default_telemetry(robot_id)
    telemetry.update(payload)
    telemetry["updated_at"] = now_iso()
    robot["telemetry"] = telemetry
    return jsonify({"ok": True, "server_time": now_iso()})


@app.get("/api/command/<robot_id>")
def get_command(robot_id: str) -> Response:
    robot = get_robot(robot_id)
    command = dict(robot["command"])
    command["server_time"] = now_iso()
    return jsonify(command)


@app.post("/api/command/<robot_id>")
def set_command(robot_id: str) -> Response:
    payload = request.get_json(force=True, silent=False) or {}
    robot = get_robot(robot_id)
    current = dict(robot["command"])

    mode = str(payload.get("mode", current["mode"]))
    if mode not in {"stop", "forward", "backward", "left", "right", "auto"}:
        mode = current["mode"]

    current["mode"] = mode
    current["speed"] = clamp_int(payload.get("speed", current["speed"]), 0, 255, current["speed"])
    current["turn"] = clamp_int(payload.get("turn", current["turn"]), 0, 255, current["turn"])
    current["buzzer"] = bool(payload.get("buzzer", current["buzzer"]))
    current["updated_at"] = now_iso()
    robot["command"] = current
    return jsonify({"ok": True, "command": current})


if __name__ == "__main__":
    app.run(host="0.0.0.0", port=8000, debug=True)
