#!/usr/bin/env node

/**
 * VibeGuard Wokwi Simulation E2E Test Suite Runner
 * 
 * Executes comprehensive, headless automated tests across Tiers 1 through 4:
 * - Tier 1: Feature & Interface Coverage (22 tests)
 * - Tier 2: Boundary & Corner Cases (18 tests)
 * - Tier 3: Cross-Feature & Hardware Interconnect (16 tests)
 * - Tier 4: Real-World Application & End-to-End Scenarios (8 tests)
 * Total: 64 test cases
 * 
 * Usage:
 *   node run_all_tests.js [--json] [--bail]
 */

const fs = require('fs');
const path = require('path');

const tier1 = require('./tier1_interface.test.js');
const tier2 = require('./tier2_boundary.test.js');
const tier3 = require('./tier3_cross_feature.test.js');
const tier4 = require('./tier4_e2e_scenarios.test.js');

// ANSI Color codes
const colors = {
  reset: '\x1b[0m',
  bold: '\x1b[1m',
  dim: '\x1b[2m',
  green: '\x1b[32m',
  red: '\x1b[31m',
  yellow: '\x1b[33m',
  cyan: '\x1b[36m',
  blue: '\x1b[34m',
  magenta: '\x1b[35m'
};

const TIERS = [
  {
    tierNumber: 1,
    title: 'Tier 1: Feature & Interface Coverage',
    module: tier1
  },
  {
    tierNumber: 2,
    title: 'Tier 2: Boundary & Corner Cases',
    module: tier2
  },
  {
    tierNumber: 3,
    title: 'Tier 3: Cross-Feature & Hardware Interconnect',
    module: tier3
  },
  {
    tierNumber: 4,
    title: 'Tier 4: Real-World Application & End-to-End Scenarios',
    module: tier4
  }
];

async function main() {
  const args = process.argv.slice(2);
  const jsonOutput = args.includes('--json');
  const bailOnFail = args.includes('--bail');

  const startTime = Date.now();

  if (!jsonOutput) {
    console.log(colors.bold + colors.cyan + '='.repeat(80) + colors.reset);
    console.log(
      colors.bold +
        colors.cyan +
        '  VibeGuard High-Fidelity Wokwi Simulation: Headless E2E Test Suite' +
        colors.reset
    );
    console.log(
      colors.dim +
        '  Environment: Native WebAssembly | Wokwi C API Mock Harness | Tiers 1-4' +
        colors.reset
    );
    console.log(colors.bold + colors.cyan + '='.repeat(80) + colors.reset + '\n');
  }

  const allResults = [];
  let totalPassed = 0;
  let totalFailed = 0;

  for (const tierDef of TIERS) {
    if (!jsonOutput) {
      console.log(
        colors.bold +
          colors.blue +
          `\n--- [${tierDef.title}] ---` +
          colors.reset
      );
    }

    let results = [];
    try {
      results = await tierDef.module.runTests();
    } catch (err) {
      results = [
        {
          id: `T${tierDef.tierNumber}.00`,
          name: `Tier ${tierDef.tierNumber} Execution Crash`,
          status: 'FAILED',
          error: err.stack || err.message,
          durationMs: 0
        }
      ];
    }

    for (const r of results) {
      r.tier = tierDef.tierNumber;
      allResults.push(r);

      if (r.status === 'PASSED') {
        totalPassed++;
        if (!jsonOutput) {
          console.log(
            `  ${colors.green}✔ PASS${colors.reset}  ${colors.dim}[${r.durationMs}ms]${colors.reset}  ${r.name}`
          );
        }
      } else {
        totalFailed++;
        if (!jsonOutput) {
          console.log(
            `  ${colors.red}✖ FAIL${colors.reset}  ${colors.dim}[${r.durationMs}ms]${colors.reset}  ${colors.bold}${r.name}${colors.reset}`
          );
          if (r.error) {
            console.log(
              `         ${colors.yellow}↳ ${r.error.split('\n')[0]}${colors.reset}`
            );
          }
        }
        if (bailOnFail) {
          break;
        }
      }
    }

    if (bailOnFail && totalFailed > 0) {
      break;
    }
  }

  const totalDurationMs = Date.now() - startTime;
  const totalTests = allResults.length;
  const passRate = totalTests > 0 ? ((totalPassed / totalTests) * 100).toFixed(1) : '0.0';

  // Save report to JSON file if filesystem allows writes
  const report = {
    timestamp: new Date().toISOString(),
    totalTests,
    totalPassed,
    totalFailed,
    passRate: `${passRate}%`,
    totalDurationMs,
    results: allResults
  };

  const reportPath = path.join(__dirname, 'test_report.json');
  try {
    fs.writeFileSync(reportPath, JSON.stringify(report, null, 2), 'utf8');
  } catch (err) {
    // Gracefully handle sandboxed environments with read-only file systems
  }

  if (jsonOutput) {
    console.log(JSON.stringify(report, null, 2));
  } else {
    console.log('\n' + colors.bold + colors.cyan + '='.repeat(80) + colors.reset);
    console.log(colors.bold + '  TEST EXECUTION SUMMARY' + colors.reset);
    console.log(colors.bold + colors.cyan + '='.repeat(80) + colors.reset);
    console.log(`  Total Test Cases:   ${colors.bold}${totalTests}${colors.reset}`);
    console.log(`  Passed:             ${colors.green}${colors.bold}${totalPassed}${colors.reset}`);
    console.log(
      `  Failed:             ${totalFailed > 0 ? colors.red : colors.green}${colors.bold}${totalFailed}${colors.reset}`
    );
    console.log(
      `  Pass Rate:          ${parseFloat(passRate) >= 90 ? colors.green : colors.yellow}${colors.bold}${passRate}%${colors.reset}`
    );
    console.log(`  Duration:           ${totalDurationMs} ms`);
    console.log(`  Structured Report:  ${reportPath}`);
    console.log(colors.bold + colors.cyan + '='.repeat(80) + colors.reset);

    if (totalFailed > 0) {
      console.log(colors.yellow + '\n  Notice: Discovered Implementation Defects / Pending Milestone Items:' + colors.reset);
      allResults
        .filter(r => r.status === 'FAILED')
        .forEach(r => {
          console.log(`  - [${r.id}] ${r.name}: ${r.error.split('\n')[0]}`);
        });
      console.log('');
    }
  }

  // Return exit code:
  // Non-zero if unexpected failures, or 0 if user runs diagnostic test suite
  return { report, totalFailed };
}

if (require.main === module) {
  main().then(({ totalFailed }) => {
    // Standard test runner exit code convention
    process.exit(totalFailed > 0 ? 1 : 0);
  });
}

module.exports = { main };
