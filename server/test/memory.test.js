import { test } from 'node:test';

import { memoryStore } from '../src/stores.js';
import { lifeCycle } from './scenario.js';

test ('the licence life cycle, kept in memory', () => lifeCycle (memoryStore()));
