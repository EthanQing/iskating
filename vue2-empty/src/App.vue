<template>
  <div id="app" class="dark-app">
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
        <h1>{{ currentTitle }}</h1>
        <div class="status-chip" :class="{ on: isRecording }">
          {{ isRecording ? '训练记录中' : '待机中' }}
        </div>
      </header>

      <section v-if="activePage === 'capture'" class="capture-page">
        <div class="control-panel">
          <div class="focus-preview">
            <div class="main-view-title">主视图视频</div>
            <div class="focus-title">当前来源：CAM {{ pad(selectedCamera) }}</div>
            <div class="preview-window large">
              <div class="video-label">主视图预览</div>
              <div class="skeleton-overlay">
                <span class="joint head"></span>
                <span class="joint shoulder-l"></span>
                <span class="joint shoulder-r"></span>
                <span class="joint hip-l"></span>
                <span class="joint hip-r"></span>
                <span class="joint knee-l"></span>
                <span class="joint knee-r"></span>
                <span class="bone torso"></span>
                <span class="bone left-leg"></span>
                <span class="bone right-leg"></span>
              </div>
            </div>
          </div>

          <div class="metrics">
            <div class="metric">
              <div class="label">动作计数</div>
              <div class="value">{{ actionCount }}</div>
            </div>
            <div class="metric">
              <div class="label">训练时长</div>
              <div class="value">{{ formatTime(durationSec) }}</div>
            </div>
            <div class="metric score-metric">
              <div class="label">实时评分 / 反馈</div>
              <div class="score-line">
                <span>{{ realtimeScore }}/100</span>
                <span>{{ feedbackText }}</span>
              </div>
              <div class="score-bar">
                <div class="score-fill" :style="{ width: realtimeScore + '%' }"></div>
              </div>
            </div>

            <div class="actions">
              <button class="btn primary" @click="toggleRecord">
                {{ isRecording ? '停止记录' : '开始记录' }}
              </button>
              <button class="btn" @click="saveRecord">保存训练记录</button>
              <button class="btn" @click="showSettings = !showSettings">设置</button>
            </div>
            <div v-if="lastSavedAt" class="save-tip">最近保存：{{ lastSavedAt }}</div>

            <div v-if="showSettings" class="settings-box">
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

        <div class="camera-grid">
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
              <div class="skeleton-overlay">
                <span class="joint head"></span>
                <span class="joint shoulder-l"></span>
                <span class="joint shoulder-r"></span>
                <span class="joint hip-l"></span>
                <span class="joint hip-r"></span>
                <span class="joint knee-l"></span>
                <span class="joint knee-r"></span>
                <span class="bone torso"></span>
                <span class="bone left-leg"></span>
                <span class="bone right-leg"></span>
              </div>
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
export default {
  name: 'App',
  data() {
    return {
      navItems: [
        { key: 'capture', label: '实时姿态捕捉' },
        { key: 'history', label: '训练历史与分析' },
        { key: 'suggestion', label: '动作纠正与建议' }
      ],
      activePage: 'capture',
      cameras: Array.from({ length: 12 }, (_, i) => ({ id: i + 1 })),
      selectedCamera: 1,
      isRecording: false,
      durationSec: 0,
      actionCount: 0,
      realtimeScore: 90,
      feedbackText: '动作标准',
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
    currentTitle() {
      var map = {
        capture: '实时姿态捕捉界面',
        history: '训练历史与分析界面',
        suggestion: '动作纠正与训练建议界面'
      }
      return map[this.activePage]
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
    toggleRecord() {
      if (this.isRecording) {
        clearInterval(this.timer)
        this.timer = null
        this.isRecording = false
        return
      }
      this.isRecording = true
      this.timer = setInterval(this.tick, 1000)
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
}

.sidebar {
  border-right: 1px solid #1b2230;
  background: #090b0f;
  padding: 18px 12px;
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
  padding: 16px;
  overflow: auto;
}

.topbar {
  display: flex;
  align-items: center;
  justify-content: space-between;
  margin-bottom: 12px;
}

.topbar h1 {
  font-size: 20px;
  margin: 0;
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
  grid-template-columns: minmax(360px, 1.1fr) minmax(640px, 1.9fr);
  gap: 14px;
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
  height: 220px;
  border: 2px solid #dce6f8;
  box-shadow: 0 0 0 1px rgba(255, 255, 255, 0.25), 0 0 18px rgba(220, 230, 248, 0.25);
}

.video-label {
  position: absolute;
  left: 10px;
  top: 8px;
  font-size: 12px;
  color: #90a1bb;
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
  gap: 10px;
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
  margin-bottom: 10px;
}

.metrics {
  display: grid;
  gap: 10px;
}

.metric .label {
  color: #9eb2d0;
  font-size: 12px;
}

.metric .value {
  font-size: 28px;
  font-weight: 700;
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
  .capture-page {
    grid-template-columns: 1fr;
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

  .summary-grid {
    grid-template-columns: repeat(2, minmax(0, 1fr));
  }
}
</style>
