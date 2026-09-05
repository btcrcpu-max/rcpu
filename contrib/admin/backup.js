const fs = require('fs');
const path = require('path');
const { execSync } = require('child_process');

const BACKUP_DIR = path.join(__dirname, 'backups');
const KEEP_BACKUPS = 10;

const filesToBackup = [
    'stratum-proxy-pool.js',
    'stratum-proxy-fixed.js',
    'server-stratum-proxy-pool.js',
    'current_proxy.js'
];

function getTimestamp() {
    const now = new Date();
    const year = now.getFullYear();
    const month = String(now.getMonth() + 1).padStart(2, '0');
    const day = String(now.getDate()).padStart(2, '0');
    const hour = String(now.getHours()).padStart(2, '0');
    const minute = String(now.getMinutes()).padStart(2, '0');
    const second = String(now.getSeconds()).padStart(2, '0');
    return `${year}${month}${day}_${hour}${minute}${second}`;
}

function log(msg) {
    const timestamp = new Date().toISOString().replace('T', ' ').substring(0, 19);
    const logMessage = `[${timestamp}] ${msg}`;
    console.log(logMessage);
    
    const logFile = path.join(BACKUP_DIR, 'backup.log');
    fs.appendFileSync(logFile, logMessage + '\n');
}

function ensureBackupDir() {
    if (!fs.existsSync(BACKUP_DIR)) {
        fs.mkdirSync(BACKUP_DIR, { recursive: true });
        log(`Created backup directory: ${BACKUP_DIR}`);
    }
}

function cleanOldBackups() {
    try {
        const entries = fs.readdirSync(BACKUP_DIR);
        const backupDirs = entries
            .filter(entry => entry.startsWith('backup_'))
            .map(entry => ({
                name: entry,
                time: fs.statSync(path.join(BACKUP_DIR, entry)).mtime.getTime()
            }))
            .sort((a, b) => b.time - a.time);

        if (backupDirs.length > KEEP_BACKUPS) {
            const toDelete = backupDirs.slice(KEEP_BACKUPS);
            toDelete.forEach(backup => {
                const backupPath = path.join(BACKUP_DIR, backup.name);
                execSync(`rmdir /s /q "${backupPath}"`, { stdio: 'ignore' });
                log(`Removed old backup: ${backup.name}`);
            });
        }
    } catch (e) {
        log(`Error cleaning old backups: ${e.message}`);
    }
}

function performBackup() {
    ensureBackupDir();
    
    const timestamp = getTimestamp();
    const backupPath = path.join(BACKUP_DIR, `backup_${timestamp}`);
    
    try {
        fs.mkdirSync(backupPath, { recursive: true });
        log(`Starting backup to: ${backupPath}`);
        
        let successCount = 0;
        let failCount = 0;
        
        filesToBackup.forEach(file => {
            const srcPath = path.join(__dirname, file);
            const dstPath = path.join(backupPath, file);
            
            if (fs.existsSync(srcPath)) {
                try {
                    fs.copyFileSync(srcPath, dstPath);
                    const stats = fs.statSync(srcPath);
                    log(`Backed up: ${file} (${stats.size} bytes)`);
                    successCount++;
                } catch (e) {
                    log(`Failed to backup ${file}: ${e.message}`);
                    failCount++;
                }
            } else {
                log(`File not found, skipping: ${file}`);
            }
        });
        
        const manifest = {
            timestamp: timestamp,
            files: filesToBackup.map(file => ({
                name: file,
                exists: fs.existsSync(path.join(__dirname, file)),
                backedUp: fs.existsSync(path.join(backupPath, file))
            })),
            totalFiles: filesToBackup.length,
            successCount: successCount,
            failCount: failCount
        };
        
        fs.writeFileSync(path.join(backupPath, 'manifest.json'), JSON.stringify(manifest, null, 2));
        log(`Backup completed. Success: ${successCount}, Failed: ${failCount}`);
        
        cleanOldBackups();
        
        return { success: true, path: backupPath, manifest };
    } catch (e) {
        log(`Backup failed: ${e.message}`);
        return { success: false, error: e.message };
    }
}

function showHelp() {
    console.log(`
RCPU Backup Script
==================

Usage:
  node backup.js           - Perform immediate backup
  node backup.js --help    - Show this help
  node backup.js --list    - List all backups
  node backup.js --restore <backup_name> <file> - Restore a specific file

Examples:
  node backup.js
  node backup.js --list
  node backup.js --restore backup_20260726_150000 stratum-proxy-pool.js
`);
}

function listBackups() {
    ensureBackupDir();
    
    try {
        const entries = fs.readdirSync(BACKUP_DIR);
        const backupDirs = entries
            .filter(entry => entry.startsWith('backup_'))
            .map(entry => ({
                name: entry,
                time: fs.statSync(path.join(BACKUP_DIR, entry)).mtime
            }))
            .sort((a, b) => b.time - a.time);
        
        console.log('\nAvailable backups:');
        console.log('-----------------');
        
        if (backupDirs.length === 0) {
            console.log('No backups found.');
        } else {
            backupDirs.forEach((backup, index) => {
                const manifestPath = path.join(BACKUP_DIR, backup.name, 'manifest.json');
                let manifestInfo = '';
                
                if (fs.existsSync(manifestPath)) {
                    try {
                        const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
                        manifestInfo = ` | Files: ${manifest.successCount}/${manifest.totalFiles}`;
                    } catch (e) {
                        // ignore
                    }
                }
                
                const timeStr = backup.time.toLocaleString();
                console.log(`${index + 1}. ${backup.name}${manifestInfo} | ${timeStr}`);
            });
        }
        console.log('');
    } catch (e) {
        console.error(`Error listing backups: ${e.message}`);
    }
}

function restoreFile(backupName, fileName) {
    ensureBackupDir();
    
    const backupPath = path.join(BACKUP_DIR, backupName);
    const srcPath = path.join(backupPath, fileName);
    const dstPath = path.join(__dirname, fileName);
    
    if (!fs.existsSync(backupPath)) {
        console.error(`Backup not found: ${backupName}`);
        return;
    }
    
    if (!fs.existsSync(srcPath)) {
        console.error(`File not found in backup: ${fileName}`);
        return;
    }
    
    try {
        if (fs.existsSync(dstPath)) {
            const backupBeforeRestore = dstPath + '.restore_backup';
            fs.copyFileSync(dstPath, backupBeforeRestore);
            console.log(`Backed up current file to: ${backupBeforeRestore}`);
        }
        
        fs.copyFileSync(srcPath, dstPath);
        console.log(`Successfully restored ${fileName} from ${backupName}`);
        log(`Restored ${fileName} from ${backupName}`);
    } catch (e) {
        console.error(`Failed to restore ${fileName}: ${e.message}`);
    }
}

const args = process.argv.slice(2);

if (args.includes('--help') || args.includes('-h')) {
    showHelp();
} else if (args.includes('--list') || args.includes('-l')) {
    listBackups();
} else if (args.includes('--restore') || args.includes('-r')) {
    const restoreIndex = args.indexOf('--restore') !== -1 ? args.indexOf('--restore') : args.indexOf('-r');
    const backupName = args[restoreIndex + 1];
    const fileName = args[restoreIndex + 2];
    
    if (backupName && fileName) {
        restoreFile(backupName, fileName);
    } else {
        console.error('Usage: node backup.js --restore <backup_name> <file>');
    }
} else {
    performBackup();
}

module.exports = { performBackup, listBackups, restoreFile };
