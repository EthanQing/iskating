<template>
  <div class="legend">
    <div class="legend-title">图例说明</div>
    <div class="legend-grid">
      <div class="legend-item">
        <span class="line track"></span>
        <span>运动轨迹</span>
      </div>
      <div class="legend-item">
        <span class="line inner"></span>
        <span>跑道内道线</span>
      </div>
      <div class="legend-item">
        <span class="line outer"></span>
        <span>跑道外道线</span>
      </div>
      <div class="legend-item">
        <span class="dot point"></span>
        <span>关键点(1-7)</span>
      </div>
      <div class="legend-item">
        <span class="arrow">➜</span>
        <span>滑行方向</span>
      </div>
      <div class="legend-item colors">
        <span class="dot c1"></span>
        <span class="dot c2"></span>
        <span class="dot c3"></span>
        <span class="dot c4"></span>
        <span class="dot c5"></span>
        <span class="dot c6"></span>
        <span class="desc">运动员位置</span>
      </div>
    </div>
  </div>
</template>

<script>
export default {
  name: 'LegendComponent'
}
</script>

<style scoped>
.legend {
  border: 1px solid #25354f;
  background: #0f1724;
  border-radius: 10px;
  padding: 10px;
}

.legend-title {
  font-size: 13px;
  font-weight: 700;
  color: #dce9ff;
  margin-bottom: 8px;
}

.legend-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 6px 12px;
}

.legend-item {
  display: flex;
  align-items: center;
  gap: 6px;
  font-size: 12px;
  color: #9fb2d2;
}

.line {
  width: 28px;
  height: 3px;
  border-radius: 999px;
  display: inline-block;
}

.line.track {
  background: linear-gradient(90deg, #2f8cff, #ff4e4e);
}

.line.inner {
  background: #8aa3c7;
}

.line.outer {
  background: #d16b6b;
}

.dot {
  width: 8px;
  height: 8px;
  border-radius: 50%;
  display: inline-block;
}

.dot.point {
  background: #eaf3ff;
  border: 1px solid #6ea2ea;
}

.arrow {
  color: #d7e8ff;
  font-size: 13px;
  line-height: 1;
}

.colors .dot {
  width: 7px;
  height: 7px;
}

.c1 { background: #2f8cff; }
.c2 { background: #4fb6ff; }
.c3 { background: #7dd37a; }
.c4 { background: #f2d05e; }
.c5 { background: #ff9f46; }
.c6 { background: #ff5e5e; }

.desc {
  margin-left: 4px;
}
</style>
