<template>
  <div id="app" class="dark-app" :class="{ 'sidebar-hidden': !sidebarVisible }">
    <aside class="sidebar">
      <div class="logo">iSkating Coach</div>
      <button
        v-for="item in navItems"
        :key="item.key"
        class="nav-btn"
        :class="{ active: activePage === item.key }"
        @click="activePage = item.key"
      >
        {{ item.label }}
      </button>
    </aside>

    <main class="content">
      <header class="topbar">
        <div class="brand-block">
          <div class="brand-logo">❄</div>
          <div class="brand-text">
            <div class="brand-title">冰刃智训</div>
            <div class="brand-subtitle">iSkating Coche</div>
          </div>
          <div class="brand-divider"></div>
          <div class="session-module">
            <div class="session-logo">⛸</div>
            <div class="session-info">
              <div class="session-title">训练轮次Session</div>
              <div class="session-subtitle">自由滑训练-第3次</div>
            </div>
          </div>
          <div class="brand-divider"></div>
          <div class="session-module">
            <div class="session-logo">🕒</div>
            <div class="session-info">
              <div class="session-title">日期/时间Data/Time</div>
              <div class="session-subtitle">2026-05-20 16：28：34</div>
            </div>
          </div>
          <div class="brand-divider"></div>
          <div class="session-module">
            <div class="session-logo">⚙</div>
            <div class="session-info">
              <div class="session-title">系统状态 System Status</div>
              <div class="session-subtitle status-ok">运行中（正常）</div>
            </div>
          </div>
          <div class="brand-divider"></div>
          <div class="session-module">
            <div class="session-logo model-logo">AI</div>
            <div class="session-info">
              <div class="session-title">模型状态 Status</div>
              <div class="session-subtitle status-ok">已就绪（v2.3.1）</div>
            </div>
          </div>
          <div class="brand-divider"></div>
          <div class="session-module">
            <div class="session-logo storage-logo">DB</div>
            <div class="session-info">
              <div class="session-title">存储Storage</div>
              <div class="session-subtitle">1.82T/4.00TB</div>
            </div>
          </div>
        </div>
        <div class="topbar-right">
          <button class="btn ghost" @click="sidebarVisible = !sidebarVisible">
            {{ sidebarVisible ? '隐藏侧栏' : '显示侧栏' }}
          </button>
        </div>
      </header>

      <section v-if="activePage === 'capture'" class="capture-page">
        <div class="capture-top-row">
          <div class="capture-col left-col">
            <div class="control-panel">
              <div class="focus-preview">
                <div class="focus-headline">
                  <div class="main-view-title">主视图视频</div>
                  <div class="focus-title">当前来源：CAM {{ pad(selectedCamera) }}</div>
                </div>
                <div class="preview-window large">
                  <div class="video-label">主视图预览</div>
                  <img class="main-preview-image" src="/skating.png" alt="主视图预览" />
                </div>
              </div>
            </div>
          </div>

          <div class="capture-col middle-col" :class="{ expanded: trajectoryExpanded }">
            <div v-show="!trajectoryExpanded" class="camera-grid">
              <div
                v-for="cam in cameras"
                :key="cam.id"
                class="camera-card"
                :class="{ selected: selectedCamera === cam.id }"
                @click="selectedCamera = cam.id"
              >
                <div class="card-head">CAM {{ pad(cam.id) }}</div>
                <div class="preview-window">
                  <div class="video-label">实时视频流</div>
                  <video class="cam-video" src="/train.mp4" autoplay muted loop playsinline></video>
                </div>
              </div>
            </div>

            <div class="insight-card trajectory-card" :class="{ expanded: trajectoryExpanded }">
              <div class="insight-title">三维轨迹</div>
              <div class="trajectory-corner-controls">
                <button class="tri-btn" type="button" @click="expandTrajectory">▲</button>
                <button class="tri-btn" type="button" @click="collapseTrajectory">▼</button>
              </div>
              <div class="trajectory-view">
                <canvas ref="trajectoryCanvas" class="track-canvas"></canvas>
              </div>
              <LegendComponent v-if="trajectoryExpanded" class="trajectory-legend" />
            </div>
          </div>

          <div class="capture-col right-col">
            <div class="ops-column">
              <div class="ops-card">
                <button class="op-btn start" @click="startCapture">
                  <span class="op-icon">▶</span>
                  <span class="op-text">开始采集</span>
                </button>
                <button class="op-btn pause" @click="pauseCapture">
                  <span class="op-icon">⏸</span>
                  <span class="op-text">暂停</span>
                </button>
                <button class="op-btn" @click="stopCapture">
                  <span class="op-icon">⏹</span>
                  <span class="op-text">停止</span>
                </button>
                <button class="op-btn" @click="saveRecord">
                  <span class="op-icon">💾</span>
                  <span class="op-text">保存记录</span>
                </button>
                <button class="op-btn" @click="showSettings = !showSettings">
                  <span class="op-icon">⚙</span>
                  <span class="op-text">系统设置</span>
                </button>
              </div>
              <div v-if="showSettings" class="settings-box ops-settings">
                <div class="setting-item">
                  <label>模型精度</label>
                  <select v-model="settings.modelPrecision">
                    <option value="high">高精度</option>
                    <option value="balanced">均衡</option>
                    <option value="fast">高速</option>
                  </select>
                </div>
                <div class="setting-item">
                  <label>帧率</label>
                  <select v-model="settings.fps">
                    <option :value="24">24 FPS</option>
                    <option :value="30">30 FPS</option>
                    <option :value="60">60 FPS</option>
                  </select>
                </div>
              </div>
            </div>
          </div>
        </div>

        <div class="capture-low-row">
          <div class="insight-card keypoint-card">
            <div class="insight-title">运动员3D姿态</div>
            <div class="pose-grid">
              <div class="pose-item">
                <img src="/pose-a.png" alt="运动员A的3D姿态" />
              </div>
              <div class="pose-item">
                <img src="/pose-b.png" alt="运动员B的3D姿态" />
              </div>
            </div>
          </div>
          <div class="insight-card low-metrics-card">
            <div class="insight-title">训练状态统计</div>
            <div class="stat-row">
              <span>动作计数</span>
              <strong>{{ actionCount }}</strong>
            </div>
            <div class="stat-row">
              <span>训练时长</span>
              <strong>{{ formatTime(durationSec) }}</strong>
            </div>
            <div class="stat-row">
              <span>实时评分 / 反馈</span>
              <strong>{{ realtimeScore }}/100 · {{ feedbackText }}</strong>
            </div>
            <div v-if="lastSavedAt" class="save-tip compact">最近保存：{{ lastSavedAt }}</div>
            <div class="mini-score-bar">
              <div class="score-fill" :style="{ width: realtimeScore + '%' }"></div>
            </div>
          </div>
        </div>
      </section>

      <section v-else-if="activePage === 'history'" class="history-page">
        <div class="summary-grid">
          <div class="summary-card">
            <div class="label">训练次数</div>
            <div class="value">{{ historyStats.sessions }}</div>
          </div>
          <div class="summary-card">
            <div class="label">总动作数</div>
            <div class="value">{{ historyStats.totalActions }}</div>
          </div>
          <div class="summary-card">
            <div class="label">平均分</div>
            <div class="value">{{ historyStats.avgScore }}</div>
          </div>
          <div class="summary-card">
            <div class="label">最佳分数</div>
            <div class="value">{{ historyStats.bestScore }}</div>
          </div>
        </div>

        <div class="history-list">
          <div v-if="!trainingRecords.length" class="empty">暂无训练记录</div>
          <div v-for="item in recordsDesc" :key="item.id" class="history-item">
            <div>
              <div class="time">{{ item.time }}</div>
              <div class="meta">时长 {{ formatTime(item.duration) }} · 动作 {{ item.actions }} 次 · CAM {{ pad(item.camera) }}</div>
            </div>
            <div class="score-tag">{{ item.score }}/100</div>
          </div>
        </div>
      </section>

      <section v-else class="suggestion-page">
        <div class="suggestion-head">
          <h2>动作纠正与训练建议</h2>
          <p>基于最近训练记录自动生成</p>
        </div>
        <div class="suggestion-list">
          <div v-for="tip in suggestions" :key="tip.title" class="suggestion-card">
            <h3>{{ tip.title }}</h3>
            <p>{{ tip.text }}</p>
          </div>
        </div>
      </section>
    </main>
  </div>
</template>

<script>
import LegendComponent from './LegendComponent.vue'

export default {
  name: 'App',
  components: {
    LegendComponent
  },
  data() {
    return {
      navItems: [
        { key: 'capture', label: '实时姿态捕捉' },
        { key: 'history', label: '训练历史与分析' },
        { key: 'suggestion', label: '动作纠正与建议' }
      ],
      activePage: 'capture',
      sidebarVisible: true,
      trajectoryExpanded: false,
      cameras: Array.from({ length: 12 }, (_, i) => ({ id: i + 1 })),
      selectedCamera: 1,
      isRecording: false,
      isPaused: false,
      durationSec: 0,
      actionCount: 0,
      realtimeScore: 90,
      feedbackText: '动作标准',
      resizeTimer: null,
      timer: null,
      showSettings: false,
      settings: {
        modelPrecision: 'balanced',
        fps: 30
      },
      trainingRecords: [],
      lastSavedAt: ''
    }
  },
  computed: {
    activeNavLabel() {
      var active = this.navItems.find(item => item.key === this.activePage)
      return active ? active.label : ''
    },
    historyStats() {
      if (!this.trainingRecords.length) {
        return { sessions: 0, totalActions: 0, avgScore: 0, bestScore: 0 }
      }
      var totalActions = this.trainingRecords.reduce(function(sum, r) {
        return sum + r.actions
      }, 0)
      var totalScore = this.trainingRecords.reduce(function(sum, r) {
        return sum + r.score
      }, 0)
      var bestScore = Math.max.apply(
        null,
        this.trainingRecords.map(function(r) {
          return r.score
        })
      )
      return {
        sessions: this.trainingRecords.length,
        totalActions: totalActions,
        avgScore: Math.round(totalScore / this.trainingRecords.length),
        bestScore: bestScore
      }
    },
    recordsDesc() {
      return this.trainingRecords.slice().reverse()
    },
    suggestions() {
      if (!this.trainingRecords.length) {
        return [
          { title: '先完成一次训练采集', text: '开始记录后系统会自动生成个性化动作纠正建议。' },
          { title: '建议采集设置', text: '模型精度建议选择“均衡”，帧率 30FPS，可兼顾识别稳定性与性能。' }
        ]
      }
      var latest = this.trainingRecords[this.trainingRecords.length - 1]
      var list = []

      if (latest.score < 85) {
        list.push({
          title: '髋膝协同不足',
          text: '最近评分偏低，建议在下肢发力阶段强调膝盖弯曲与髋部同步伸展，降低动作迟滞。'
        })
      } else {
        list.push({
          title: '动作整体稳定',
          text: '最新训练评分较高，可适当提升节奏，保持动作幅度一致性。'
        })
      }

      if (latest.actions < 20) {
        list.push({
          title: '训练量偏少',
          text: '建议每组动作次数提升至 20 次以上，便于模型获得更稳定的评估结果。'
        })
      } else {
        list.push({
          title: '训练量合理',
          text: '当前训练量达标，建议增加多角度摄像头交叉校验，提高纠错精度。'
        })
      }

      list.push({
        title: '下次训练参数建议',
        text: '保持 ' + this.settings.fps + 'FPS，模型精度选择“' + this.precisionLabel(this.settings.modelPrecision) + '”，并优先关注 CAM ' + this.pad(latest.camera) + ' 视角。'
      })

      return list
    }
  },
  watch: {
    activePage(value) {
      if (value !== 'capture') return
      var self = this
      this.$nextTick(function() {
        self.drawTrajectoryCanvas()
      })
    },
    trajectoryExpanded() {
      var self = this
      this.$nextTick(function() {
        self.drawTrajectoryCanvas()
      })
    }
  },
  mounted() {
    var self = this
    this.$nextTick(function() {
      self.drawTrajectoryCanvas()
    })
    window.addEventListener('resize', this.onResize)
  },
  methods: {
    pad(num) {
      return String(num).padStart(2, '0')
    },
    precisionLabel(value) {
      var map = {
        high: '高精度',
        balanced: '均衡',
        fast: '高速'
      }
      return map[value] || value
    },
    formatTime(sec) {
      var mm = String(Math.floor(sec / 60)).padStart(2, '0')
      var ss = String(sec % 60).padStart(2, '0')
      return mm + ':' + ss
    },
    startCapture() {
      if (this.isRecording && !this.isPaused) return
      this.isRecording = true
      this.isPaused = false
      if (this.timer) clearInterval(this.timer)
      this.timer = setInterval(this.tick, 1000)
    },
    pauseCapture() {
      if (!this.isRecording || this.isPaused) return
      this.isPaused = true
      if (this.timer) {
        clearInterval(this.timer)
        this.timer = null
      }
    },
    stopCapture() {
      if (this.timer) {
        clearInterval(this.timer)
        this.timer = null
      }
      this.isRecording = false
      this.isPaused = false
    },
    expandTrajectory() {
      this.trajectoryExpanded = true
      var self = this
      this.$nextTick(function() {
        self.drawTrajectoryCanvas()
      })
    },
    collapseTrajectory() {
      this.trajectoryExpanded = false
      var self = this
      this.$nextTick(function() {
        self.drawTrajectoryCanvas()
      })
    },
    onResize() {
      var self = this
      if (this.resizeTimer) clearTimeout(this.resizeTimer)
      this.resizeTimer = setTimeout(function() {
        self.drawTrajectoryCanvas()
      }, 80)
    },
    drawTrajectoryCanvas() {
      var canvas = this.$refs.trajectoryCanvas
      if (!canvas) return
      var ctx = canvas.getContext('2d')
      if (!ctx) return

      var w = canvas.clientWidth
      var h = canvas.clientHeight
      if (!w || !h) return
      var dpr = window.devicePixelRatio || 1
      canvas.width = Math.round(w * dpr)
      canvas.height = Math.round(h * dpr)
      ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
      ctx.clearRect(0, 0, w, h)

      // 尺寸参数（参考 public/track.htm 的赛道结构）
      var cx = w / 2
      var cy = h / 2
      var straightLen = Math.max(70, w * 0.22)
      var trackRadius = Math.max(38, h * 0.29)

      // 背景：中心亮、四周渐隐入黑色
      ctx.fillStyle = '#0b0f16'
      ctx.fillRect(0, 0, w, h)
      var bg = ctx.createRadialGradient(cx, cy, Math.min(w, h) * 0.12, cx, cy, Math.max(w, h) * 0.62)
      bg.addColorStop(0, '#f6f9fc')
      bg.addColorStop(0.55, '#edf4fa')
      bg.addColorStop(0.78, 'rgba(177, 198, 221, 0.34)')
      bg.addColorStop(1, 'rgba(11, 15, 22, 0.96)')
      ctx.fillStyle = bg
      ctx.fillRect(0, 0, w, h)

      function drawOval(halfWidth, radius, color, lineWidth) {
        ctx.beginPath()
        ctx.arc(cx + halfWidth, cy, radius, -Math.PI / 2, Math.PI / 2)
        ctx.lineTo(cx - halfWidth, cy + radius)
        ctx.arc(cx - halfWidth, cy, radius, Math.PI / 2, -Math.PI / 2)
        ctx.closePath()
        ctx.strokeStyle = color
        ctx.lineWidth = lineWidth
        ctx.stroke()
      }

      // 赛道层
      drawOval(straightLen, trackRadius + 26, '#1c5b9e', 5)
      drawOval(straightLen, trackRadius + 18, '#d2e4f6', 2)
      drawOval(straightLen, trackRadius + 6, '#a5c9f1', 1)
      drawOval(straightLen, trackRadius - 6, '#a5c9f1', 1)
      drawOval(straightLen, trackRadius - 28, '#f0433d', 1)

      // 辅助虚线 + 刻度点
      ctx.strokeStyle = '#a5c9f1'
      ctx.lineWidth = 1
      ctx.setLineDash([4, 4])
      ;[-straightLen, 0, straightLen].forEach(function(x) {
        ctx.beginPath()
        ctx.moveTo(cx + x, cy - trackRadius - 26)
        ctx.lineTo(cx + x, cy + trackRadius + 26)
        ctx.stroke()
        ctx.fillStyle = '#f0433d'
        ctx.beginPath()
        ctx.arc(cx + x, cy, 2.2, 0, Math.PI * 2)
        ctx.fill()
      })
      ctx.setLineDash([])

      // 渐变轨迹
      var r = trackRadius - 10
      var points = []
      var x = 0
      for (x = straightLen - 8; x >= -straightLen; x -= 1.8) points.push({ x: cx + x, y: cy + r })
      var a = 0
      for (a = Math.PI / 2; a <= Math.PI * 1.5; a += 0.03) {
        points.push({ x: cx - straightLen + Math.cos(a) * r, y: cy + Math.sin(a) * r })
      }
      for (x = -straightLen; x <= straightLen; x += 1.8) points.push({ x: cx + x, y: cy - r })
      for (a = -Math.PI / 2; a <= Math.PI * 0.1; a += 0.03) {
        points.push({ x: cx + straightLen + Math.cos(a) * r, y: cy + Math.sin(a) * r })
      }

      ctx.lineWidth = Math.max(3.5, Math.min(6.5, h * 0.045))
      ctx.lineCap = 'round'
      ctx.lineJoin = 'round'
      var i = 1
      for (i = 1; i < points.length; i++) {
        var t = i / points.length
        var hue = 240 * (1 - t)
        ctx.strokeStyle = 'hsl(' + hue + ', 100%, 50%)'
        ctx.beginPath()
        ctx.moveTo(points[i - 1].x, points[i - 1].y)
        ctx.lineTo(points[i].x, points[i].y)
        ctx.stroke()
      }

      // 标记点
      ;[0.72, 0.76, 0.8, 0.84, 0.88, 0.92].forEach(function(t, idx) {
        var p = points[Math.floor(t * (points.length - 1))]
        if (!p) return
        var hue = 240 * (1 - t)
        ctx.fillStyle = '#fff'
        ctx.beginPath()
        ctx.arc(p.x, p.y, 6.8, 0, Math.PI * 2)
        ctx.fill()
        ctx.fillStyle = 'hsl(' + hue + ', 100%, 45%)'
        ctx.beginPath()
        ctx.arc(p.x, p.y, 5.2, 0, Math.PI * 2)
        ctx.fill()
        ctx.fillStyle = '#fff'
        ctx.font = 'bold 9px Arial'
        ctx.textAlign = 'center'
        ctx.textBaseline = 'middle'
        ctx.fillText(String(idx + 1), p.x, p.y + 0.5)
      })

      // 标签
      function drawTag(px, py, text, color, ox, oy) {
        ctx.fillStyle = color
        ctx.beginPath()
        ctx.arc(px, py, 5, 0, Math.PI * 2)
        ctx.fill()
        ctx.strokeStyle = '#fff'
        ctx.lineWidth = 1.5
        ctx.stroke()

        ctx.strokeStyle = color
        ctx.beginPath()
        ctx.moveTo(px, py + 6)
        ctx.lineTo(px + ox, py + oy)
        ctx.stroke()

        var bw = 34
        var bh = 18
        var bx = px + ox - bw / 2
        var by = py + oy
        ctx.fillStyle = color
        if (ctx.roundRect) {
          ctx.beginPath()
          ctx.roundRect(bx, by, bw, bh, 4)
          ctx.fill()
        } else {
          ctx.fillRect(bx, by, bw, bh)
        }
        ctx.fillStyle = '#fff'
        ctx.font = 'bold 11px "Microsoft YaHei"'
        ctx.textAlign = 'center'
        ctx.textBaseline = 'middle'
        ctx.fillText(text, bx + bw / 2, by + bh / 2 + 0.5)
      }

      drawTag(cx + straightLen - 8, cy + r, '起点', '#0145db', 0, 12)
      var endA = Math.PI * 0.1
      drawTag(cx + straightLen + Math.cos(endA) * r, cy + Math.sin(endA) * r, '终点', '#d31000', 18, 2)
    },
    tick() {
      this.durationSec += 1
      if (this.durationSec % 2 === 0) {
        this.actionCount += 1
      }
      var delta = Math.floor(Math.random() * 7) - 3
      var next = this.realtimeScore + delta
      this.realtimeScore = Math.max(70, Math.min(98, next))
      if (this.realtimeScore >= 90) {
        this.feedbackText = '动作标准'
      } else if (this.realtimeScore >= 80) {
        this.feedbackText = '继续优化'
      } else {
        this.feedbackText = '需要纠正'
      }
    },
    saveRecord() {
      if (!this.durationSec && !this.actionCount) return
      var record = {
        id: Date.now(),
        time: new Date().toLocaleString(),
        duration: this.durationSec,
        actions: this.actionCount,
        score: this.realtimeScore,
        camera: this.selectedCamera,
        videoPath: '/records/session-' + Date.now() + '.mp4',
        skeletonFrames: this.durationSec * this.settings.fps,
        modelPrecision: this.settings.modelPrecision,
        fps: this.settings.fps
      }
      this.trainingRecords.push(record)
      this.lastSavedAt = record.time
    }
  },
  beforeDestroy() {
    window.removeEventListener('resize', this.onResize)
    if (this.resizeTimer) clearTimeout(this.resizeTimer)
    if (this.timer) clearInterval(this.timer)
  }
}
</script>

<style>
* {
  box-sizing: border-box;
}

html,
body,
#app {
  margin: 0;
  width: 100%;
  height: 100%;
  font-family: "Inter", "PingFang SC", "Microsoft YaHei", sans-serif;
  background: #0b0d11;
  color: #e7edf7;
}

.dark-app {
  display: grid;
  grid-template-columns: 220px 1fr;
  height: 100%;
  transition: grid-template-columns 0.25s ease;
}

.dark-app.sidebar-hidden {
  grid-template-columns: 0 1fr;
}

.sidebar {
  border-right: 1px solid #1b2230;
  background: #090b0f;
  padding: 18px 12px;
  overflow: hidden;
  transition: all 0.25s ease;
}

.dark-app.sidebar-hidden .sidebar {
  padding: 0;
  border-right: none;
}

.logo {
  font-weight: 700;
  font-size: 18px;
  margin-bottom: 18px;
}

.nav-btn {
  width: 100%;
  border: 1px solid #1c2534;
  background: #101623;
  color: #cdd8ea;
  border-radius: 10px;
  padding: 10px 12px;
  margin-bottom: 10px;
  cursor: pointer;
  text-align: left;
}

.nav-btn.active {
  background: #15335c;
  border-color: #2c78e6;
  color: #ffffff;
}

.content {
  padding: 24px 16px 16px;
  overflow: auto;
}

.topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 22px;
}

.brand-block {
  display: flex;
  align-items: center;
  gap: 10px;
}

.brand-logo {
  width: 40px;
  height: 40px;
  border-radius: 10px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 22px;
  background: linear-gradient(135deg, #213c64, #0f1d33);
  border: 1px solid #3d6ba8;
  color: #dfeeff;
}

.brand-title {
  font-size: 20px;
  font-weight: 700;
  color: #eaf2ff;
  line-height: 1.1;
}

.brand-subtitle {
  margin-top: 2px;
  font-size: 12px;
  color: #8ea9cc;
}

.brand-divider {
  width: 1px;
  height: 42px;
  background: #7fb6ff66;
  margin: 0 4px 0 6px;
}

.session-module {
  display: flex;
  align-items: center;
  gap: 8px;
}

.session-logo {
  width: 34px;
  height: 34px;
  border-radius: 8px;
  display: flex;
  align-items: center;
  justify-content: center;
  font-size: 18px;
  border: 1px solid #5f8fc7;
  background: #11223b;
  color: #cfe6ff;
}

.model-logo {
  font-size: 12px;
  font-weight: 700;
  letter-spacing: 0.5px;
  color: #d9ebff;
  border-color: #6ea3e6;
  background: #122845;
}

.storage-logo {
  font-size: 11px;
  font-weight: 700;
  letter-spacing: 0.4px;
  color: #d7ecff;
  border-color: #6b97c7;
  background: #16253a;
}

.session-title {
  font-size: 12px;
  color: #9dc3f5;
  line-height: 1.1;
}

.session-subtitle {
  margin-top: 3px;
  font-size: 13px;
  color: #e8f2ff;
  line-height: 1.1;
}

.status-ok {
  color: #63e28b;
}

.topbar-right {
  display: flex;
  align-items: center;
  gap: 10px;
}

.page-chip {
  padding: 6px 10px;
  border-radius: 999px;
  font-size: 12px;
  color: #9ec4ff;
  border: 1px solid #31578f;
  background: #10203a;
}

.status-chip {
  padding: 6px 12px;
  border-radius: 999px;
  background: #232a37;
  color: #b5c2d8;
  font-size: 12px;
}

.status-chip.on {
  background: #1f4f31;
  color: #8ef5b2;
}

.capture-page {
  display: grid;
  grid-template-rows: auto auto;
  gap: 14px;
  align-items: start;
}

.capture-top-row {
  display: grid;
  grid-template-columns: minmax(500px, 1.56fr) minmax(460px, 1.26fr) minmax(104px, 0.18fr);
  gap: 14px;
  align-items: stretch;
}

.capture-low-row {
  display: grid;
  grid-template-columns: minmax(500px, 1.56fr) minmax(460px, 1.26fr) minmax(104px, 0.18fr);
  gap: 14px;
  align-items: stretch;
  height: 240px;
  max-height: 240px;
}

.capture-col {
  display: flex;
  flex-direction: column;
  gap: 14px;
  min-height: 100%;
}

.capture-top-row .left-col .control-panel {
  height: 100%;
}

.capture-top-row .left-col .focus-preview {
  height: 100%;
  display: flex;
  flex-direction: column;
}

.camera-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 10px;
}

.camera-card {
  border: 1px solid #202a3a;
  background: #0f141d;
  border-radius: 10px;
  padding: 8px;
  cursor: pointer;
}

.camera-card.selected {
  border-color: #2f7ff5;
}

.card-head {
  font-size: 12px;
  color: #9eb2d0;
  margin-bottom: 6px;
}

.preview-window {
  position: relative;
  border-radius: 8px;
  overflow: hidden;
  background: linear-gradient(135deg, #1a202c, #0e131b);
  height: 110px;
  border: 1px solid #1c2432;
}

.preview-window.large {
  height: 340px;
  border: 2px solid #dce6f8;
  box-shadow: 0 0 0 1px rgba(255, 255, 255, 0.25), 0 0 18px rgba(220, 230, 248, 0.25);
}

.capture-top-row .left-col .preview-window.large {
  flex: 1;
  height: auto;
  min-height: 520px;
}

.video-label {
  position: absolute;
  left: 10px;
  top: 8px;
  font-size: 12px;
  color: #90a1bb;
  z-index: 2;
}

.cam-video {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

.main-preview-image {
  width: 100%;
  height: 100%;
  object-fit: cover;
  display: block;
}

.skeleton-overlay {
  position: absolute;
  inset: 0;
}

.joint,
.bone {
  position: absolute;
  display: block;
}

.joint {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: #63e1ff;
  box-shadow: 0 0 8px #63e1ff;
}

.head {
  top: 25%;
  left: 50%;
}

.shoulder-l {
  top: 38%;
  left: 40%;
}

.shoulder-r {
  top: 38%;
  left: 60%;
}

.hip-l {
  top: 56%;
  left: 45%;
}

.hip-r {
  top: 56%;
  left: 55%;
}

.knee-l {
  top: 73%;
  left: 42%;
}

.knee-r {
  top: 73%;
  left: 58%;
}

.bone {
  height: 2px;
  transform-origin: left center;
  background: #8aa7ff;
  box-shadow: 0 0 8px #8aa7ff;
}

.torso {
  top: 48%;
  left: 45%;
  width: 12%;
  transform: rotate(90deg);
}

.left-leg {
  top: 62%;
  left: 46%;
  width: 11%;
  transform: rotate(120deg);
}

.right-leg {
  top: 62%;
  left: 54%;
  width: 11%;
  transform: rotate(60deg);
}

.control-panel {
  display: grid;
  gap: 8px;
}

.ops-column {
  display: flex;
  flex-direction: column;
  gap: 10px;
  height: 100%;
}

.insight-card {
  border: 1px solid #1d2737;
  background: #0f141d;
  border-radius: 12px;
  padding: 12px;
}

.keypoint-card {
  grid-column: 1 / 2;
  min-height: 0;
  height: 100%;
  overflow: auto;
}

.low-metrics-card {
  grid-column: 2 / 4;
  min-height: 0;
  height: 100%;
  overflow: auto;
  display: grid;
  grid-auto-rows: min-content;
  gap: 6px;
}

.trajectory-card {
  min-height: 188px;
  position: relative;
}

.middle-col.expanded .trajectory-card {
  flex: 1;
  display: flex;
  flex-direction: column;
}

.trajectory-card.expanded .trajectory-view {
  flex: 1;
  height: auto;
  min-height: 430px;
}

.trajectory-legend {
  margin-top: 10px;
}

.insight-title {
  font-size: 15px;
  font-weight: 700;
  color: #eaf2ff;
  margin-bottom: 10px;
}

.pose-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 8px;
  height: 130px;
}

.pose-item {
  border: 1px solid #26344b;
  border-radius: 8px;
  overflow: hidden;
  height: 100%;
  background:
    radial-gradient(
      circle at center,
      #f6f9fc 0%,
      #f3f8fc 76%,
      rgba(229, 238, 247, 0.72) 92%,
      rgba(206, 221, 236, 0.42) 98%,
      rgba(171, 190, 211, 0.16) 100%
    );
}

.pose-item img {
  width: 100%;
  height: 100%;
  object-fit: contain;
  object-position: center;
  background: transparent;
  display: block;
}

.stat-row {
  border: 1px solid #223047;
  border-radius: 8px;
  background: #111925;
  padding: 5px 8px;
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  font-size: 11px;
  color: #9eb2d0;
}

.stat-row strong {
  font-size: 14px;
  color: #e7edf7;
  font-weight: 700;
}

.mini-score-bar {
  width: 100%;
  height: 6px;
  background: #1f2735;
  border-radius: 999px;
  overflow: hidden;
}

.trajectory-view {
  position: relative;
  height: 134px;
  border: 1px solid #26334a;
  border-radius: 10px;
  background:
    linear-gradient(to right, rgba(103, 139, 194, 0.12) 1px, transparent 1px),
    linear-gradient(to bottom, rgba(103, 139, 194, 0.12) 1px, transparent 1px),
    #101724;
  background-size: 24px 24px, 24px 24px, auto;
  overflow: hidden;
}

.track-canvas {
  width: 100%;
  height: 100%;
  display: block;
}

.trajectory-corner-controls {
  position: absolute;
  top: 10px;
  right: 12px;
  display: flex;
  flex-direction: row;
  gap: 6px;
  z-index: 3;
}

.tri-btn {
  width: 26px;
  height: 22px;
  border: 1px solid #4b6f9f;
  background: rgba(15, 24, 37, 0.85);
  color: #d8e8ff;
  border-radius: 6px;
  cursor: pointer;
  line-height: 1;
  padding: 0;
}

.axis,
.track-line {
  position: absolute;
  display: block;
}

.axis {
  height: 2px;
  transform-origin: left center;
  background: #88a8d8;
  opacity: 0.9;
}

.axis.x {
  left: 16px;
  bottom: 18px;
  width: 120px;
}

.axis.y {
  left: 16px;
  bottom: 18px;
  width: 82px;
  transform: rotate(-73deg);
}

.axis.z {
  left: 16px;
  bottom: 18px;
  width: 86px;
  transform: rotate(28deg);
}

.track-line {
  height: 2px;
  border-radius: 999px;
  background: #8fd2ff;
  box-shadow: 0 0 8px rgba(143, 210, 255, 0.8);
}

.track-line.t1 {
  width: 140px;
  left: 74px;
  top: 56px;
  transform: rotate(-14deg);
}

.track-line.t2 {
  width: 120px;
  left: 136px;
  top: 66px;
  transform: rotate(17deg);
}

.track-line.t3 {
  width: 98px;
  left: 216px;
  top: 78px;
  transform: rotate(-11deg);
}

.ops-card {
  border: 2px solid #dce6f8;
  box-shadow: 0 0 0 1px rgba(255, 255, 255, 0.25), 0 0 18px rgba(220, 230, 248, 0.2);
  background: #0f141d;
  border-radius: 12px;
  padding: 12px 10px;
  display: flex;
  flex-direction: column;
  gap: 10px;
  flex: 1;
}

.op-btn {
  width: 100%;
  min-height: 72px;
  border: 1px solid #324258;
  background: #121a27;
  color: #d7e4fb;
  border-radius: 10px;
  cursor: pointer;
  display: flex;
  align-items: center;
  justify-content: center;
  flex-direction: column;
  gap: 6px;
  flex: 1;
}

.op-btn.start {
  border-color: #4a84d3;
  background: #173764;
}

.op-icon {
  font-size: 28px;
  line-height: 1;
}

.op-text {
  font-size: 12px;
}

.ops-settings {
  border-color: #dce6f8;
}

.focus-preview,
.metrics,
.summary-card,
.history-item,
.suggestion-card {
  border: 1px solid #1d2737;
  background: #0f141d;
  border-radius: 12px;
  padding: 12px;
}

.focus-title {
  font-size: 13px;
  color: #9bb0cf;
  margin-bottom: 0;
}

.focus-headline {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 10px;
  margin-bottom: 10px;
}

.main-view-title {
  display: inline-block;
  font-size: 15px;
  font-weight: 700;
  color: #eaf2ff;
  padding: 6px 12px;
  border-left: 3px solid #3b8dff;
  border-radius: 8px;
  background: #111d31;
  margin-bottom: 0;
}

.metrics {
  display: grid;
  gap: 6px;
  margin-top: 6px;
}

.metric {
  border: 1px solid #223047;
  border-radius: 9px;
  background: #111925;
  padding: 6px 10px;
}

.metric .label {
  color: #9eb2d0;
  font-size: 12px;
}

.metric .value {
  font-size: 20px;
  font-weight: 700;
}

.metric.action-metric {
  padding: 6px 10px;
}

.metric.action-metric .value {
  font-size: 20px;
  line-height: 1.1;
}

.score-line {
  display: flex;
  justify-content: space-between;
  margin-bottom: 8px;
}

.score-bar {
  width: 100%;
  height: 8px;
  background: #1f2735;
  border-radius: 999px;
  overflow: hidden;
}

.score-fill {
  height: 100%;
  background: linear-gradient(90deg, #2d8cff, #65e1ff);
}

.actions {
  display: flex;
  gap: 8px;
  flex-wrap: wrap;
}

.save-tip {
  font-size: 12px;
  color: #8db2ef;
}

.save-tip.compact {
  font-size: 11px;
}

.btn {
  border: 1px solid #2b3447;
  background: #141b27;
  color: #d7e4fb;
  padding: 8px 10px;
  border-radius: 8px;
  cursor: pointer;
}

.btn.primary {
  background: #1e57b3;
  border-color: #2f7ff5;
  color: #fff;
}

.btn.ghost {
  background: #111823;
}

.settings-box {
  border: 1px solid #243149;
  background: #121927;
  border-radius: 10px;
  padding: 10px;
  display: grid;
  gap: 10px;
}

.setting-item {
  display: grid;
  gap: 6px;
}

.setting-item label {
  font-size: 12px;
  color: #9fb0c8;
}

.setting-item select {
  background: #0f1520;
  color: #e7edf7;
  border: 1px solid #2b3447;
  border-radius: 6px;
  padding: 6px;
}

.summary-grid {
  display: grid;
  grid-template-columns: repeat(4, minmax(0, 1fr));
  gap: 12px;
  margin-bottom: 12px;
}

.summary-card .label {
  color: #9eb2d0;
  font-size: 12px;
}

.summary-card .value {
  font-size: 30px;
  font-weight: 700;
}

.history-list {
  display: grid;
  gap: 10px;
}

.history-item {
  display: flex;
  justify-content: space-between;
  align-items: center;
}

.history-item .time {
  font-weight: 600;
}

.history-item .meta {
  font-size: 12px;
  color: #9eb2d0;
  margin-top: 4px;
}

.score-tag {
  background: #15335c;
  color: #8cc1ff;
  border: 1px solid #2f7ff5;
  border-radius: 8px;
  padding: 6px 10px;
  font-weight: 700;
}

.empty {
  padding: 30px;
  text-align: center;
  color: #92a4c2;
  border: 1px dashed #2b3447;
  border-radius: 10px;
}

.suggestion-head h2 {
  margin: 0;
}

.suggestion-head p {
  margin: 6px 0 12px;
  color: #9eb2d0;
}

.suggestion-list {
  display: grid;
  gap: 10px;
}

.suggestion-card h3 {
  margin: 0 0 6px;
  font-size: 16px;
}

.suggestion-card p {
  margin: 0;
  color: #b8c7dd;
  line-height: 1.5;
}

@media (max-width: 1360px) {
  .capture-top-row,
  .capture-low-row {
    grid-template-columns: 1fr;
  }

  .capture-low-row {
    max-height: none;
  }

  .keypoint-card {
    grid-column: auto;
  }

  .low-metrics-card {
    grid-column: auto;
  }
}

@media (max-width: 1000px) {
  .dark-app {
    grid-template-columns: 1fr;
  }

  .sidebar {
    display: flex;
    gap: 8px;
    align-items: center;
    overflow: auto;
  }

  .logo {
    margin-bottom: 0;
    margin-right: 10px;
    white-space: nowrap;
  }

  .nav-btn {
    margin-bottom: 0;
    min-width: 150px;
  }

  .camera-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }

  .capture-top-row,
  .capture-low-row {
    grid-template-columns: 1fr;
  }

  .summary-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}
</style>
