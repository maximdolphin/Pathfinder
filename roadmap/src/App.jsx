import { useCallback, useEffect, useMemo, useState } from 'react';

// The viewer is deliberately read-only.
//
// Every mutation goes through scripts/task.mjs, which means the roadmap has
// exactly one writer and the file is always in a state the agent produced. A UI
// with buttons would invite a second writer and the two would disagree; this way
// the JSON on disk is the truth and the page is a window onto it.
//
// It polls rather than using a websocket, because the whole backend is a file.

const POLL_MS = 2500;

const STATUS_ORDER = { active: 0, blocked: 1, todo: 2, done: 3 };

function useRoadmap() {
  const [roadmap, setRoadmap] = useState(null);
  const [error, setError] = useState(null);

  const fetchRoadmap = useCallback(async () => {
    try {
      // Cache-busted: the file changes under us and a 304 would hide it.
      const response = await fetch(`roadmap.json?t=${Date.now()}`);
      if (!response.ok) throw new Error(`${response.status} ${response.statusText}`);
      setRoadmap(await response.json());
      setError(null);
    } catch (cause) {
      setError(cause.message);
    }
  }, []);

  useEffect(() => {
    fetchRoadmap();
    const timer = setInterval(fetchRoadmap, POLL_MS);
    return () => clearInterval(timer);
  }, [fetchRoadmap]);

  return { roadmap, error };
}

function Bar({ value, total, tone = 'accent' }) {
  const fraction = total > 0 ? value / total : 0;
  return (
    <div className="bar">
      <div className={`bar-fill tone-${tone}`} style={{ width: `${fraction * 100}%` }} />
    </div>
  );
}

function StatusDot({ status }) {
  return <span className={`dot dot-${status}`} title={status} />;
}

function Evidence({ items }) {
  if (!items || items.length === 0) return null;
  return (
    <div className="evidence">
      {items.map((item) => {
        const isImage = /\.(png|jpg|jpeg|gif|webp)$/i.test(item.file);
        return (
          <figure key={item.file} className="evidence-item">
            {isImage ? (
              <a href={item.file} target="_blank" rel="noreferrer">
                <img src={item.file} alt={item.caption} loading="lazy" />
              </a>
            ) : (
              <a className="evidence-file" href={item.file} target="_blank" rel="noreferrer">
                {item.file.split('/').pop()}
              </a>
            )}
            <figcaption>
              {item.caption}
              <span className="evidence-date">{item.addedAt?.slice(0, 10)}</span>
            </figcaption>
          </figure>
        );
      })}
    </div>
  );
}

function TaskRow({ task, expanded, onToggle }) {
  return (
    <li className={`task task-${task.status}`}>
      <button className="task-head" onClick={onToggle} aria-expanded={expanded}>
        <StatusDot status={task.status} />
        <span className="task-id">{task.id}</span>
        <span className="task-title">{task.title}</span>
        <span className="task-days">{task.estimateDays}d</span>
      </button>
      {expanded && (
        <div className="task-body">
          <p className="task-detail">{task.detail}</p>
          <p className="task-acceptance">
            <span className="label">acceptance</span>
            {task.acceptance}
          </p>
          <p className="task-meta">
            <span>planned {task.plannedStart} → {task.plannedEnd}</span>
            {task.designRefs.length > 0 && <span>design {task.designRefs.join(', ')}</span>}
            {task.startedAt && <span>started {task.startedAt}</span>}
            {task.completedAt && <span>done {task.completedAt}</span>}
          </p>
          {task.notes.length > 0 && (
            <ul className="notes">
              {task.notes.map((note, index) => (
                <li key={index} className={`note note-${note.kind}`}>
                  <span className="note-date">{note.at}</span>
                  {note.text}
                </li>
              ))}
            </ul>
          )}
          <Evidence items={task.evidence} />
        </div>
      )}
    </li>
  );
}

function MilestonePanel({ milestone, tasks }) {
  const [expandedTask, setExpandedTask] = useState(null);
  const [filter, setFilter] = useState('all');

  const done = tasks.filter((task) => task.status === 'done').length;
  const visible = useMemo(() => {
    const list = filter === 'all' ? tasks : tasks.filter((task) => task.status === filter);
    return [...list].sort((a, b) => {
      const byStatus = STATUS_ORDER[a.status] - STATUS_ORDER[b.status];
      return byStatus !== 0 ? byStatus : a.order - b.order;
    });
  }, [tasks, filter]);

  return (
    <section className="panel">
      <header className="panel-head">
        <div>
          <h2>
            <span className="milestone-id">{milestone.id}</span>
            {milestone.title}
          </h2>
          <p className="goal">{milestone.goal}</p>
        </div>
        <div className="panel-stats">
          <div className="count">{done}<span>/{tasks.length}</span></div>
          <div className="weeks">weeks {milestone.weekStart}–{milestone.weekEnd}</div>
        </div>
      </header>

      <Bar value={done} total={tasks.length} tone={milestone.gatePassedAt ? 'done' : 'accent'} />

      <div className={`gate ${milestone.gatePassedAt ? 'gate-passed' : ''}`}>
        <div className="gate-label">
          GATE
          {milestone.gatePassedAt && <span className="gate-stamp">passed {milestone.gatePassedAt}</span>}
        </div>
        <p>{milestone.gate}</p>
        {milestone.gateNote && <p className="gate-note">{milestone.gateNote}</p>}
        <Evidence items={milestone.evidence} />
      </div>

      <div className="filters">
        {['all', 'active', 'blocked', 'todo', 'done'].map((option) => (
          <button
            key={option}
            className={filter === option ? 'chip chip-on' : 'chip'}
            onClick={() => setFilter(option)}
          >
            {option}
            <span className="chip-count">
              {option === 'all' ? tasks.length : tasks.filter((t) => t.status === option).length}
            </span>
          </button>
        ))}
      </div>

      <ul className="tasks">
        {visible.map((task) => (
          <TaskRow
            key={task.id}
            task={task}
            expanded={expandedTask === task.id}
            onToggle={() => setExpandedTask(expandedTask === task.id ? null : task.id)}
          />
        ))}
      </ul>
    </section>
  );
}

export default function App() {
  const { roadmap, error } = useRoadmap();
  const [selected, setSelected] = useState(null);

  const summary = useMemo(() => {
    if (!roadmap) return null;
    const total = roadmap.tasks.length;
    const done = roadmap.tasks.filter((task) => task.status === 'done').length;
    const days = roadmap.tasks.reduce((sum, task) => sum + task.estimateDays, 0);
    const daysDone = roadmap.tasks
      .filter((task) => task.status === 'done')
      .reduce((sum, task) => sum + task.estimateDays, 0);
    const active = roadmap.tasks.find((task) => task.status === 'active');
    const nextUp = roadmap.tasks.find((task) => task.status === 'todo');
    const blocked = roadmap.tasks.filter((task) => task.status === 'blocked');
    return { total, done, days, daysDone, active, nextUp, blocked };
  }, [roadmap]);

  // Default to whatever is actually in flight, falling back to the first
  // milestone that is not finished. Opening on M0 every time would be useless.
  useEffect(() => {
    if (!roadmap || selected) return;
    const active = roadmap.tasks.find((task) => task.status === 'active');
    if (active) {
      setSelected(active.milestone);
      return;
    }
    const open = roadmap.milestones.find((milestone) => !milestone.gatePassedAt);
    setSelected(open ? open.id : roadmap.milestones[0].id);
  }, [roadmap, selected]);

  if (error && !roadmap) {
    return (
      <div className="shell">
        <div className="empty">
          <h1>roadmap.json unreadable</h1>
          <p>{error}</p>
          <p className="hint">run <code>python scripts/generate_roadmap.py</code></p>
        </div>
      </div>
    );
  }

  if (!roadmap || !summary) {
    return <div className="shell"><div className="empty">loading…</div></div>;
  }

  const current = roadmap.milestones.find((milestone) => milestone.id === selected);
  const currentTasks = roadmap.tasks.filter((task) => task.milestone === selected);

  return (
    <div className="shell">
      <aside className="rail">
        <div className="brand">
          <h1>{roadmap.project}</h1>
          <p>{roadmap.subtitle}</p>
        </div>

        <div className="totals">
          <div className="total-row">
            <span>tasks</span>
            <strong>{summary.done}<em>/{summary.total}</em></strong>
          </div>
          <Bar value={summary.done} total={summary.total} />
          <div className="total-row">
            <span>effort</span>
            <strong>{summary.daysDone}<em>/{summary.days} days</em></strong>
          </div>
          <Bar value={summary.daysDone} total={summary.days} tone="muted" />
        </div>

        {summary.active && (
          <button className="now now-active" onClick={() => setSelected(summary.active.milestone)}>
            <span className="now-label">in flight</span>
            <span className="now-id">{summary.active.id}</span>
            {summary.active.title}
          </button>
        )}
        {!summary.active && summary.nextUp && (
          <button className="now" onClick={() => setSelected(summary.nextUp.milestone)}>
            <span className="now-label">next up</span>
            <span className="now-id">{summary.nextUp.id}</span>
            {summary.nextUp.title}
          </button>
        )}
        {summary.blocked.length > 0 && (
          <div className="blocked-banner">
            {summary.blocked.length} blocked
          </div>
        )}

        <nav className="milestones">
          {roadmap.milestones.map((milestone) => {
            const owned = roadmap.tasks.filter((task) => task.milestone === milestone.id);
            const done = owned.filter((task) => task.status === 'done').length;
            const state = milestone.gatePassedAt
              ? 'done'
              : done === owned.length && owned.length > 0
                ? 'pending'
                : done > 0
                  ? 'active'
                  : 'todo';
            return (
              <button
                key={milestone.id}
                className={`ms ms-${state} ${selected === milestone.id ? 'ms-on' : ''}`}
                onClick={() => setSelected(milestone.id)}
              >
                <span className="ms-mark" />
                <span className="ms-id">{milestone.id}</span>
                <span className="ms-title">{milestone.title}</span>
                <span className="ms-count">{done}/{owned.length}</span>
              </button>
            );
          })}
        </nav>

        <footer className="rail-foot">
          <div>read-only view · managed by <code>scripts/task.mjs</code></div>
          <div>{roadmap.updatedAt ? `updated ${roadmap.updatedAt.slice(0, 19).replace('T', ' ')}` : `generated ${roadmap.generatedAt}`}</div>
          {error && <div className="stale">stale: {error}</div>}
        </footer>
      </aside>

      <main className="detail">
        {current && <MilestonePanel milestone={current} tasks={currentTasks} />}
      </main>
    </div>
  );
}
