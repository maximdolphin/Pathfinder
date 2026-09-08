#!/usr/bin/env node
// The agent's interface to the roadmap.
//
// The React app is a window; this is the handle. Everything the roadmap knows
// lives in public/roadmap.json, and every mutation goes through here so the file
// stays well-formed and every change is timestamped.
//
// Design note: there is no server and no database. The data is a JSON file in a
// git repository, which means every status change is a diff, every diff has an
// author and a date, and the history of the project is the history of the file.
// A database would have been more machinery and less accountability.
//
// Usage:
//   node scripts/task.mjs status
//   node scripts/task.mjs next
//   node scripts/task.mjs list --milestone M1 --status todo
//   node scripts/task.mjs show T042
//   node scripts/task.mjs start T042
//   node scripts/task.mjs done T042 --note "landed in c58cf58"
//   node scripts/task.mjs block T042 --reason "waiting on IP counsel"
//   node scripts/task.mjs unblock T042
//   node scripts/task.mjs note T042 "measured 4.2 ms per patch"
//   node scripts/task.mjs evidence T042 ../out/terrain-orbit.png --caption "6,371 km"
//   node scripts/task.mjs gate M1                     show the checks
//   node scripts/task.mjs gate M1 --pass --checked 1,2,3 --note "..."

import { readFileSync, writeFileSync, copyFileSync, mkdirSync, existsSync } from 'node:fs';
import { basename, dirname, extname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(HERE, '..');
const DATA = join(ROOT, 'public', 'roadmap.json');
const EVIDENCE_DIR = join(ROOT, 'public', 'evidence');

const STATUSES = ['todo', 'active', 'blocked', 'done'];

function load() {
  return JSON.parse(readFileSync(DATA, 'utf8'));
}

function save(roadmap) {
  roadmap.updatedAt = new Date().toISOString();
  writeFileSync(DATA, JSON.stringify(roadmap, null, 1) + '\n', 'utf8');
}

function today() {
  return new Date().toISOString().slice(0, 10);
}

/** Parses `--flag value` and bare positionals out of argv. */
function parseArgs(argv) {
  const flags = {};
  const positional = [];
  for (let index = 0; index < argv.length; index += 1) {
    const token = argv[index];
    if (token.startsWith('--')) {
      const name = token.slice(2);
      const next = argv[index + 1];
      if (next === undefined || next.startsWith('--')) {
        flags[name] = true;
      } else {
        flags[name] = next;
        index += 1;
      }
    } else {
      positional.push(token);
    }
  }
  return { flags, positional };
}

function findTask(roadmap, id) {
  const task = roadmap.tasks.find((entry) => entry.id === id.toUpperCase());
  if (!task) throw new Error(`no task ${id}`);
  return task;
}

function findMilestone(roadmap, id) {
  const milestone = roadmap.milestones.find((entry) => entry.id === id.toUpperCase());
  if (!milestone) throw new Error(`no milestone ${id}`);
  return milestone;
}

/** A milestone is done when every task in it is. Derived, never stored twice. */
function refreshMilestones(roadmap) {
  for (const milestone of roadmap.milestones) {
    const owned = roadmap.tasks.filter((task) => task.milestone === milestone.id);
    const done = owned.filter((task) => task.status === 'done').length;
    const active = owned.some((task) => task.status === 'active');
    milestone.doneCount = done;
    if (owned.length > 0 && done === owned.length) {
      if (milestone.status !== 'done') milestone.status = 'complete-pending-gate';
    } else if (active || done > 0) {
      milestone.status = 'active';
    } else {
      milestone.status = 'todo';
    }
    // A passed gate outranks the derived state: the gate is the real checkpoint,
    // and finishing the tasks is only evidence that it might be passable.
    if (milestone.gatePassedAt) milestone.status = 'done';
  }
}

function bar(fraction, width = 28) {
  const filled = Math.round(fraction * width);
  return '#'.repeat(filled) + '.'.repeat(width - filled);
}

// ---------------------------------------------------------------- commands

const commands = {
  status(roadmap) {
    const total = roadmap.tasks.length;
    const done = roadmap.tasks.filter((t) => t.status === 'done').length;
    const active = roadmap.tasks.filter((t) => t.status === 'active');
    const blocked = roadmap.tasks.filter((t) => t.status === 'blocked');
    const daysTotal = roadmap.tasks.reduce((sum, t) => sum + t.estimateDays, 0);
    const daysDone = roadmap.tasks
      .filter((t) => t.status === 'done')
      .reduce((sum, t) => sum + t.estimateDays, 0);

    console.log(`\n${roadmap.project} — ${roadmap.subtitle}\n`);
    console.log(`  tasks   ${bar(done / total)}  ${done}/${total}  (${((done / total) * 100).toFixed(1)}%)`);
    console.log(`  effort  ${bar(daysDone / daysTotal)}  ${daysDone}/${daysTotal} days\n`);

    for (const milestone of roadmap.milestones) {
      const owned = roadmap.tasks.filter((t) => t.milestone === milestone.id);
      const complete = owned.filter((t) => t.status === 'done').length;
      const mark = milestone.gatePassedAt ? 'x' : complete === owned.length ? '~' : ' ';
      const evidence = milestone.evidence?.length ? ` [${milestone.evidence.length} evidence]` : '';
      console.log(
        `  [${mark}] ${milestone.id.padEnd(3)} ${milestone.title.padEnd(34)}` +
        ` ${String(complete).padStart(3)}/${String(owned.length).padEnd(3)}` +
        ` wk ${milestone.weekStart}-${milestone.weekEnd}${evidence}`
      );
    }

    if (active.length) {
      console.log('\n  active:');
      for (const task of active) console.log(`    ${task.id}  ${task.title}`);
    }
    if (blocked.length) {
      console.log('\n  blocked:');
      for (const task of blocked) {
        const reason = task.notes.filter((n) => n.kind === 'block').slice(-1)[0];
        console.log(`    ${task.id}  ${task.title}${reason ? ` — ${reason.text}` : ''}`);
      }
    }
    console.log();
  },

  next(roadmap) {
    const active = roadmap.tasks.find((t) => t.status === 'active');
    if (active) {
      console.log(`already active: ${active.id}`);
      commands.show(roadmap, { positional: [active.id], flags: {} });
      return;
    }
    // Linear plan: the next task is the first that is neither done nor blocked.
    const next = roadmap.tasks.find((t) => t.status === 'todo');
    if (!next) {
      console.log('nothing left. every task is done or blocked.');
      return;
    }
    commands.show(roadmap, { positional: [next.id], flags: {} });
  },

  list(roadmap, { flags }) {
    let tasks = roadmap.tasks;
    if (flags.milestone) tasks = tasks.filter((t) => t.milestone === String(flags.milestone).toUpperCase());
    if (flags.status) tasks = tasks.filter((t) => t.status === flags.status);
    if (flags.search) {
      const needle = String(flags.search).toLowerCase();
      tasks = tasks.filter((t) => (t.title + t.detail).toLowerCase().includes(needle));
    }
    const limit = flags.limit ? Number(flags.limit) : 60;
    for (const task of tasks.slice(0, limit)) {
      const mark = { done: 'x', active: '>', blocked: '!', todo: ' ' }[task.status];
      console.log(`  [${mark}] ${task.id}  ${task.milestone.padEnd(3)}  ${task.title}`);
    }
    console.log(`\n  ${tasks.length} matching${tasks.length > limit ? ` (showing ${limit})` : ''}`);
  },

  show(roadmap, { positional }) {
    const id = positional[0];
    if (!id) throw new Error('show needs an id');

    if (/^M\d+$/i.test(id)) {
      const milestone = findMilestone(roadmap, id);
      const owned = roadmap.tasks.filter((t) => t.milestone === milestone.id);
      console.log(`\n${milestone.id} — ${milestone.title}`);
      console.log(`\n  goal   ${milestone.goal}`);
      console.log(`\n  GATE   ${milestone.gate}`);
      console.log(`\n  weeks ${milestone.weekStart}-${milestone.weekEnd}` +
        `   ${milestone.plannedStart} to ${milestone.plannedEnd}` +
        `   ${owned.filter((t) => t.status === 'done').length}/${owned.length} tasks`);
      console.log(`  design ${milestone.designRefs.join(', ')}`);
      if (milestone.gatePassedAt) console.log(`  passed ${milestone.gatePassedAt}`);
      if (milestone.evidence?.length) {
        console.log('\n  evidence:');
        for (const item of milestone.evidence) console.log(`    ${item.file} — ${item.caption}`);
      }
      console.log();
      return;
    }

    const task = findTask(roadmap, id);
    console.log(`\n${task.id}  [${task.status}]  ${task.milestone}`);
    console.log(`\n  ${task.title}`);
    console.log(`\n  ${task.detail}`);
    console.log(`\n  ACCEPTANCE  ${task.acceptance}`);
    console.log(`\n  estimate ${task.estimateDays}d   planned ${task.plannedStart} to ${task.plannedEnd}`);
    if (task.designRefs.length) console.log(`  design   ${task.designRefs.join(', ')}`);
    if (task.startedAt) console.log(`  started  ${task.startedAt}`);
    if (task.completedAt) console.log(`  done     ${task.completedAt}`);
    for (const note of task.notes) console.log(`  note     [${note.at}] ${note.text}`);
    for (const item of task.evidence) console.log(`  evidence ${item.file} — ${item.caption}`);
    console.log();
  },

  start(roadmap, { positional }) {
    const task = findTask(roadmap, positional[0]);
    const alreadyActive = roadmap.tasks.find((t) => t.status === 'active' && t.id !== task.id);
    if (alreadyActive) {
      // One at a time. A plan with three things in flight and one developer is
      // not a plan, it is three half-finished things.
      throw new Error(`${alreadyActive.id} is already active; finish or block it first`);
    }
    task.status = 'active';
    task.startedAt = task.startedAt || today();
    console.log(`started ${task.id}  ${task.title}`);
    return true;
  },

  done(roadmap, { positional, flags }) {
    const task = findTask(roadmap, positional[0]);
    task.status = 'done';
    task.completedAt = today();
    if (flags.note) task.notes.push({ at: today(), kind: 'done', text: String(flags.note) });
    console.log(`done ${task.id}  ${task.title}`);

    const owned = roadmap.tasks.filter((t) => t.milestone === task.milestone);
    if (owned.every((t) => t.status === 'done')) {
      const milestone = findMilestone(roadmap, task.milestone);
      console.log(`\n  ${milestone.id} tasks all complete. GATE:`);
      console.log(`  ${milestone.gate}`);
      console.log(`\n  attach proof, then: task gate ${milestone.id} --pass\n`);
    }
    return true;
  },

  block(roadmap, { positional, flags }) {
    const task = findTask(roadmap, positional[0]);
    if (!flags.reason) throw new Error('block needs --reason');
    task.status = 'blocked';
    task.notes.push({ at: today(), kind: 'block', text: String(flags.reason) });
    console.log(`blocked ${task.id} — ${flags.reason}`);
    return true;
  },

  unblock(roadmap, { positional }) {
    const task = findTask(roadmap, positional[0]);
    task.status = task.startedAt ? 'active' : 'todo';
    task.notes.push({ at: today(), kind: 'unblock', text: 'unblocked' });
    console.log(`unblocked ${task.id} (now ${task.status})`);
    return true;
  },

  note(roadmap, { positional }) {
    const task = findTask(roadmap, positional[0]);
    const text = positional.slice(1).join(' ');
    if (!text) throw new Error('note needs text');
    task.notes.push({ at: today(), kind: 'note', text });
    console.log(`noted on ${task.id}`);
    return true;
  },

  evidence(roadmap, { positional, flags }) {
    const [id, source] = positional;
    if (!id || !source) throw new Error('evidence needs an id and a file path');

    const absolute = resolve(process.cwd(), source);
    if (!existsSync(absolute)) throw new Error(`no such file: ${absolute}`);

    mkdirSync(EVIDENCE_DIR, { recursive: true });
    // Millisecond resolution plus a collision guard: attaching six files in one
    // shell loop happens inside a single second, and second-resolution names
    // silently overwrote four of them.
    const stamp = new Date().toISOString().replace(/[:.]/g, '-').replace('Z', '');
    const extension = extname(absolute) || '.bin';
    let name = `${id.toUpperCase()}-${stamp}${extension}`;
    let attempt = 1;
    while (existsSync(join(EVIDENCE_DIR, name))) {
      name = `${id.toUpperCase()}-${stamp}-${attempt}${extension}`;
      attempt += 1;
    }
    copyFileSync(absolute, join(EVIDENCE_DIR, name));

    // Copied rather than referenced: proof that lives outside the repository is
    // proof that can quietly disappear, and a milestone whose evidence has
    // vanished is a milestone nobody can check.
    const record = {
      file: `evidence/${name}`,
      caption: flags.caption ? String(flags.caption) : basename(absolute),
      source,
      addedAt: new Date().toISOString(),
    };

    const target = /^M\d+$/i.test(id) ? findMilestone(roadmap, id) : findTask(roadmap, id);
    target.evidence = target.evidence || [];
    target.evidence.push(record);
    console.log(`evidence attached to ${target.id}: ${record.file}`);
    return true;
  },

  gate(roadmap, { positional, flags }) {
    const milestone = findMilestone(roadmap, positional[0]);
    const owned = roadmap.tasks.filter((t) => t.milestone === milestone.id);
    const outstanding = owned.filter((t) => t.status !== 'done');

    if (flags.pass) {
      if (outstanding.length) {
        // The gate is not a formality to be waved through with work outstanding.
        throw new Error(
          `${milestone.id} has ${outstanding.length} unfinished tasks; ` +
          `first is ${outstanding[0].id}. Finish or drop them before passing the gate.`
        );
      }
      if (!milestone.evidence?.length) {
        throw new Error(`${milestone.id} has no evidence attached. A gate without proof is an opinion.`);
      }

      // Every check has to be answered, individually, in writing.
      //
      // The point of the checklist is that a gate cannot be passed on a general
      // impression that things went well. `--checked` takes the indices that
      // hold; anything unanswered is named here rather than being quietly
      // included in a summary sentence.
      const checks = milestone.gateChecks || [];
      if (checks.length) {
        const answered = String(flags.checked || '')
          .split(',')
          .map((n) => Number.parseInt(n.trim(), 10))
          .filter((n) => Number.isInteger(n) && n >= 1 && n <= checks.length);
        const unanswered = checks
          .map((check, index) => ({ check, number: index + 1 }))
          .filter((entry) => !answered.includes(entry.number));

        if (unanswered.length) {
          const lines = unanswered
            .map((entry) => `    ${entry.number}. [${entry.check.how}] ${entry.check.check}`)
            .join('\n');
          throw new Error(
            `${milestone.id}: ${unanswered.length} of ${checks.length} gate checks ` +
            `are unanswered.\n\n${lines}\n\n` +
            `  Each one is met or it is not. Pass the numbers that hold:\n` +
            `    task gate ${milestone.id} --pass --checked 1,2,3,...\n\n` +
            `  A check that cannot be met is a milestone that is not finished, ` +
            `or a check that was wrong. Both are worth saying out loud.`
          );
        }
        milestone.gateChecksAnsweredAt = today();
      }

      milestone.gatePassedAt = today();
      milestone.status = 'done';
      if (flags.note) {
        milestone.gateNote = String(flags.note);
      }
      console.log(`gate passed: ${milestone.id} — ${milestone.title}`);
      console.log(`  ${checks.length} checks answered, ${milestone.evidence.length} evidence items`);
      return true;
    }

    console.log(`\n${milestone.id} GATE\n\n  ${milestone.gate}\n`);
    for (const [index, check] of (milestone.gateChecks || []).entries()) {
      console.log(`  ${String(index + 1).padStart(2)}. [${check.how.padEnd(8)}] ${check.check}`);
    }
    console.log(`\n  ${owned.length - outstanding.length}/${owned.length} tasks done`);
    console.log(`  ${milestone.evidence?.length || 0} evidence items attached\n`);
    return false;
  },
};

// ---------------------------------------------------------------- entry

function main() {
  const [, , command, ...rest] = process.argv;
  if (!command || command === 'help' || command === '--help') {
    console.log(readFileSync(fileURLToPath(import.meta.url), 'utf8')
      .split('\n')
      .filter((line) => line.startsWith('//'))
      .map((line) => line.replace(/^\/\/ ?/, ''))
      .join('\n'));
    return;
  }

  const handler = commands[command];
  if (!handler) {
    console.error(`unknown command: ${command}. try: ${Object.keys(commands).join(', ')}`);
    process.exitCode = 1;
    return;
  }

  const roadmap = load();
  const parsed = parseArgs(rest);
  const mutated = handler(roadmap, parsed);
  if (mutated) {
    refreshMilestones(roadmap);
    save(roadmap);
  }
}

try {
  main();
} catch (error) {
  console.error(`error: ${error.message}`);
  process.exitCode = 1;
}
