"use strict";
const $ = (id) => document.getElementById(id),
  types = { keyboard: "键盘", mouse: "鼠标", gamepad: "手柄" };
let backend,
  request = 0,
  original = null,
  selected = null,
  busy = false;
const local = (d) => {
  const n = new Date(d.getTime() - d.getTimezoneOffset() * 60000);
  return n.toISOString().slice(0, 19);
};
const fmt = (n) => BigInt(n).toLocaleString("zh-CN");
function preset(name) {
  let start = new Date(),
    end = new Date();
  start.setHours(0, 0, 0, 0);
  end.setHours(24, 0, 0, 0);
  if (name === "yesterday") {
    end = new Date(start);
    start.setDate(start.getDate() - 1);
  }
  if (name === "week") start.setDate(start.getDate() - 6);
  if (name === "all") {
    start = new Date(0);
    end = new Date("2099-01-01T00:00:00");
  }
  $("start").value = local(start);
  $("end").value = local(end);
  document
    .querySelectorAll("[data-range]")
    .forEach((b) => b.classList.toggle("active", b.dataset.range === name));
  original = null;
  selected = null;
  refresh();
}
function filter() {
  return {
    start: Math.floor(new Date($("start").value).getTime() / 1000),
    end: Math.floor(new Date($("end").value).getTime() / 1000),
    type: $("type").value,
    app: $("app").value,
    request: ++request,
  };
}
function message(text) {
  $("message").hidden = !text;
  $("message").textContent = text;
}
function refresh() {
  if (!backend) return;
  const f = filter();
  if (
    !Number.isFinite(f.start) ||
    !Number.isFinite(f.end) ||
    f.start >= f.end
  ) {
    message("请选择有效时间，起始时间必须早于结束时间。");
    return;
  }
  busy = true;
  backend.queryStats(JSON.stringify(f));
  $("range").textContent =
    `${$("start").value.replace("T", " ")} → ${$("end").value.replace("T", " ")}（不含终点）`;
  $("back").hidden = !original;
}
function options(id, items, defaultName) {
  const el = $(id),
    old = el.value;
  el.replaceChildren(new Option(defaultName, ""));
  items.forEach((i) => el.add(new Option(i.name, i.id)));
  el.value = old;
  if (el.selectedIndex < 0) el.value = "";
}
function render(data) {
  if (data.request !== request) return;
  busy = false;
  $("status").textContent = data.status;
  if (data.error) {
    message(
      "数据读取失败：" + data.error + "。保留上次结果；请确认采集器已启动。",
    );
    return;
  }
  message("");
  options("app", data.apps, "全部软件");
  $("total").textContent = fmt(data.total);
  $("updated").textContent = "最后查询：" + data.updated;
  $("rowcount").textContent = data.rows.length + " 个按键";
  $("rows").replaceChildren();
  const total = BigInt(data.total);
  data.rows.forEach((r) => {
    let tr = document.createElement("tr");
    let td = document.createElement("td"),
      badge = document.createElement("span");
    badge.className = "badge";
    badge.textContent = types[r.type];
    td.append(badge, document.createTextNode(r.name));
    tr.append(td);
    [
      types[r.type],
      fmt(r.count),
      total ? Number((BigInt(r.count) * 10000n) / total) / 100 + "%" : "0%",
    ].forEach((t) => {
      let c = document.createElement("td");
      c.textContent = t;
      tr.append(c);
    });
    $("rows").append(tr);
  });
  if (!data.rows.length) {
    let tr = document.createElement("tr"),
      td = document.createElement("td");
    td.colSpan = 4;
    td.className = "empty";
    td.textContent = "这个时段没有输入";
    tr.append(td);
    $("rows").append(tr);
  }
  // Keep the original chart while an hour is selected.
  if (!original) {
    $("chart").replaceChildren();
    let max = Math.max(1, ...data.bars.map((b) => Number(b.count)));
    data.bars.forEach((b) => {
      let bar = document.createElement("button");
      bar.className = "bar";
      bar.style.height = Math.max(3, (Number(b.count) / max) * 200) + "px";
      bar.title = b.label + " · " + fmt(b.count) + " 次";
      bar.setAttribute("aria-label", bar.title);
      bar.onclick = () => {
        original = { start: $("start").value, end: $("end").value };
        $("start").value = local(new Date(b.start * 1000));
        $("end").value = local(new Date(b.end * 1000));
        bar.classList.add("selected");
        refresh();
      };
      $("chart").append(bar);
    });
    if (!data.bars.length) {
      let e = document.createElement("div");
      e.className = "empty";
      e.textContent = "这个时段没有输入";
      $("chart").append(e);
    }
  }
  $("software").replaceChildren();
  data.software.forEach((r) => {
    let d = document.createElement("div");
    d.className = "app-row";
    for (let value of [r.name, fmt(r.count)]) {
      let s = document.createElement("span");
      s.textContent = value;
      d.append(s);
    }
    $("software").append(d);
  });
}
new QWebChannel(qt.webChannelTransport, (channel) => {
  backend = channel.objects.backend;
  backend.result.connect((s) => render(JSON.parse(s)));
  backend.notice.connect(message);
  preset("today");
  setInterval(() => {
    if (!busy) refresh();
  }, 5000);
});
document
  .querySelectorAll("[data-range]")
  .forEach((b) => (b.onclick = () => preset(b.dataset.range)));
$("apply").onclick = () => {
  original = null;
  document
    .querySelectorAll("[data-range]")
    .forEach((b) => b.classList.remove("active"));
  refresh();
};
$("type").onchange = refresh;
$("app").onchange = refresh;
$("back").onclick = () => {
  if (original) {
    $("start").value = original.start;
    $("end").value = original.end;
    original = null;
    refresh();
  }
};
$("export").onclick = () => {
  let f = filter();
  --request;
  backend.exportCsv(JSON.stringify(f));
};
$("clear").onclick = () => backend.clearHistory();
