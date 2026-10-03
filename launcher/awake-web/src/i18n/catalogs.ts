export const en = {
  library: 'Instances', search: 'Search instances', filters: 'View options', sort: 'Sort by', name: 'Name', lastLaunch: 'Last played', playTime: 'Play time',
  group: 'Group', allGroups: 'All groups', pinnedOnly: 'Pinned only', compact: 'Compact list', reducedMotion: 'Reduce motion', systemMotion: 'Your system also reduces motion.',
  create: 'Create instance', import: 'Import', accounts: 'Accounts', settings: 'Settings', application: 'Application', logs: 'Logs', legacy: 'Open classic interface',
  edit: 'Edit instance', folder: 'Open folder', manage: 'Manage instance', launchOptions: 'Launch options', play: 'Play', running: 'Running', broken: 'Needs repair', unavailable: 'Launch unavailable', ready: 'Ready to play',
  pin: 'Pin instance', unpin: 'Unpin instance', pinned: 'Pinned', more: 'More actions', minecraft: 'Minecraft', loading: 'Loading instances…', working: 'Working…',
  emptyTitle: 'No instances yet', emptyBody: 'Create or import an instance to start playing.', noResults: 'No matching instances', clearFilters: 'Clear filters', choose: 'Select an instance',
  disconnected: 'The launcher connection is unavailable.', timeout: 'The launcher did not respond in time.', protocol: 'The launcher sent an invalid response.', operation: 'The action could not be completed.', transport: 'The launcher connection failed.',
  artworkError: 'The screenshot could not be loaded.', artworkLoading: 'Loading screenshot…', noArtwork: 'No screenshot available', technicalDetails: 'Technical details', retry: 'Retry', dismiss: 'Dismiss',
  accountUnavailable: 'No active account', connectionTitle: 'Unable to load the library', openLauncher: 'Open this interface in Awake Launcher, then retry.', errorTitle: 'Action failed', selected: 'Selected instance',
  lastPlayed: 'Last played', totalTime: 'Play time', seconds: 'seconds', skipToPlay: 'Skip to Play', filtersActive: 'Filters active', artworkRetry: 'Retry screenshot',
} as const;
export type MessageKey = keyof typeof en;
type Catalog = Record<MessageKey, string>;
export const th: Catalog = {
  library: 'อินสแตนซ์', search: 'ค้นหาอินสแตนซ์', filters: 'ตัวเลือกมุมมอง', sort: 'เรียงตาม', name: 'ชื่อ', lastLaunch: 'เล่นล่าสุด', playTime: 'เวลาเล่น',
  group: 'กลุ่ม', allGroups: 'ทุกกลุ่ม', pinnedOnly: 'เฉพาะที่ปักหมุด', compact: 'รายการแบบย่อ', reducedMotion: 'ลดการเคลื่อนไหว', systemMotion: 'ระบบของคุณเปิดการลดการเคลื่อนไหวด้วย',
  create: 'สร้างอินสแตนซ์', import: 'นำเข้า', accounts: 'บัญชี', settings: 'การตั้งค่า', application: 'แอปพลิเคชัน', logs: 'บันทึก', legacy: 'เปิดหน้าตาแบบเดิม',
  edit: 'แก้ไขอินสแตนซ์', folder: 'เปิดโฟลเดอร์', manage: 'จัดการอินสแตนซ์', launchOptions: 'ตัวเลือกการเปิดเกม', play: 'เล่น', running: 'กำลังทำงาน', broken: 'ต้องแก้ไข', unavailable: 'ไม่สามารถเปิดเกมได้', ready: 'พร้อมเล่น',
  pin: 'ปักหมุดอินสแตนซ์', unpin: 'เลิกปักหมุดอินสแตนซ์', pinned: 'ปักหมุดแล้ว', more: 'การทำงานเพิ่มเติม', minecraft: 'Minecraft', loading: 'กำลังโหลดอินสแตนซ์…', working: 'กำลังดำเนินการ…',
  emptyTitle: 'ยังไม่มีอินสแตนซ์', emptyBody: 'สร้างหรือนำเข้าอินสแตนซ์เพื่อเริ่มเล่น', noResults: 'ไม่พบอินสแตนซ์ที่ตรงกัน', clearFilters: 'ล้างตัวกรอง', choose: 'เลือกอินสแตนซ์',
  disconnected: 'ไม่สามารถเชื่อมต่อกับตัวเปิดเกมได้', timeout: 'ตัวเปิดเกมไม่ตอบกลับภายในเวลาที่กำหนด', protocol: 'ตัวเปิดเกมส่งข้อมูลตอบกลับที่ไม่ถูกต้อง', operation: 'ไม่สามารถทำงานนี้ให้เสร็จได้', transport: 'การเชื่อมต่อกับตัวเปิดเกมล้มเหลว',
  artworkError: 'ไม่สามารถโหลดภาพหน้าจอได้', artworkLoading: 'กำลังโหลดภาพหน้าจอ…', noArtwork: 'ไม่มีภาพหน้าจอ', technicalDetails: 'รายละเอียดทางเทคนิค', retry: 'ลองอีกครั้ง', dismiss: 'ปิด',
  accountUnavailable: 'ไม่มีบัญชีที่ใช้งานอยู่', connectionTitle: 'ไม่สามารถโหลดคลังอินสแตนซ์ได้', openLauncher: 'เปิดหน้าจอนี้ใน Awake Launcher แล้วลองอีกครั้ง', errorTitle: 'การทำงานล้มเหลว', selected: 'อินสแตนซ์ที่เลือก',
  lastPlayed: 'เล่นล่าสุด', totalTime: 'เวลาเล่น', seconds: 'วินาที', skipToPlay: 'ข้ามไปที่ปุ่มเล่น', filtersActive: 'กำลังใช้ตัวกรอง', artworkRetry: 'โหลดภาพหน้าจออีกครั้ง',
};
export const zhCN: Catalog = {
  library: '实例', search: '搜索实例', filters: '视图选项', sort: '排序方式', name: '名称', lastLaunch: '最近游玩', playTime: '游玩时长',
  group: '分组', allGroups: '全部分组', pinnedOnly: '仅显示置顶', compact: '紧凑列表', reducedMotion: '减少动态效果', systemMotion: '系统也已启用减少动态效果。',
  create: '创建实例', import: '导入', accounts: '账户', settings: '设置', application: '应用', logs: '日志', legacy: '打开经典界面',
  edit: '编辑实例', folder: '打开文件夹', manage: '管理实例', launchOptions: '启动选项', play: '游玩', running: '运行中', broken: '需要修复', unavailable: '无法启动', ready: '可以游玩',
  pin: '置顶实例', unpin: '取消置顶', pinned: '已置顶', more: '更多操作', minecraft: 'Minecraft', loading: '正在加载实例…', working: '正在处理…',
  emptyTitle: '尚无实例', emptyBody: '创建或导入实例以开始游玩。', noResults: '没有匹配的实例', clearFilters: '清除筛选', choose: '选择一个实例',
  disconnected: '无法连接启动器。', timeout: '启动器未在规定时间内响应。', protocol: '启动器返回了无效响应。', operation: '无法完成此操作。', transport: '启动器连接失败。',
  artworkError: '无法加载截图。', artworkLoading: '正在加载截图…', noArtwork: '没有可用截图', technicalDetails: '技术详情', retry: '重试', dismiss: '关闭',
  accountUnavailable: '没有当前账户', connectionTitle: '无法加载实例库', openLauncher: '请在 Awake Launcher 中打开此界面，然后重试。', errorTitle: '操作失败', selected: '所选实例',
  lastPlayed: '最近游玩', totalTime: '游玩时长', seconds: '秒', skipToPlay: '跳至游玩按钮', filtersActive: '已启用筛选', artworkRetry: '重新加载截图',
};
export const zhTW: Catalog = {
  library: '實例', search: '搜尋實例', filters: '檢視選項', sort: '排序方式', name: '名稱', lastLaunch: '最近遊玩', playTime: '遊玩時間',
  group: '群組', allGroups: '所有群組', pinnedOnly: '僅顯示釘選', compact: '精簡清單', reducedMotion: '減少動態效果', systemMotion: '系統也已啟用減少動態效果。',
  create: '建立實例', import: '匯入', accounts: '帳號', settings: '設定', application: '應用程式', logs: '記錄', legacy: '開啟傳統介面',
  edit: '編輯實例', folder: '開啟資料夾', manage: '管理實例', launchOptions: '啟動選項', play: '遊玩', running: '執行中', broken: '需要修復', unavailable: '無法啟動', ready: '可以遊玩',
  pin: '釘選實例', unpin: '取消釘選', pinned: '已釘選', more: '更多操作', minecraft: 'Minecraft', loading: '正在載入實例…', working: '正在處理…',
  emptyTitle: '尚無實例', emptyBody: '建立或匯入實例以開始遊玩。', noResults: '沒有符合條件的實例', clearFilters: '清除篩選', choose: '選擇一個實例',
  disconnected: '無法連線至啟動器。', timeout: '啟動器未在指定時間內回應。', protocol: '啟動器傳回了無效回應。', operation: '無法完成此操作。', transport: '啟動器連線失敗。',
  artworkError: '無法載入螢幕截圖。', artworkLoading: '正在載入螢幕截圖…', noArtwork: '沒有可用的螢幕截圖', technicalDetails: '技術詳細資訊', retry: '重試', dismiss: '關閉',
  accountUnavailable: '沒有使用中的帳號', connectionTitle: '無法載入實例庫', openLauncher: '請在 Awake Launcher 中開啟此介面，然後重試。', errorTitle: '操作失敗', selected: '所選實例',
  lastPlayed: '最近遊玩', totalTime: '遊玩時間', seconds: '秒', skipToPlay: '跳至遊玩按鈕', filtersActive: '已啟用篩選', artworkRetry: '重新載入螢幕截圖',
};
export type Locale = 'en' | 'th' | 'zh-CN' | 'zh-TW';
export const catalogs: Record<Locale, Catalog> = { en, th, 'zh-CN': zhCN, 'zh-TW': zhTW };
export function normalizeLocale(input: string): Locale {
  const value = input.replaceAll('_', '-').toLowerCase();
  if (value === 'th' || value.startsWith('th-')) return 'th';
  if (value.startsWith('zh')) return /(?:tw|hk|mo|hant)/.test(value) ? 'zh-TW' : 'zh-CN';
  return 'en';
}
