param([string]$Client,[string]$Fixture,[string]$OutputDir,[switch]$Baseline,[switch]$Switcher,[switch]$Fresh,[switch]$SavedLogin,[switch]$ChannelIcons,[switch]$VoiceRejoin,[switch]$AudioSelect,[switch]$ServerReturn,[switch]$Settings,[switch]$Invitations,[switch]$LinkCover,[switch]$IdleMemory)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName UIAutomationClient,UIAutomationTypes,System.Drawing,System.Security,System.Windows.Forms
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class NimboWindowQA {
 [StructLayout(LayoutKind.Sequential)] public struct Rect {public int x,y,right,bottom;}
 [StructLayout(LayoutKind.Sequential)] public struct Point {public int x,y;}
 [StructLayout(LayoutKind.Sequential)] public struct GuiThread {public int size,flags;public IntPtr active,focus,capture,menuOwner,moveSize,caret;public Rect caretRect;}
 [DllImport("user32")] public static extern uint GetWindowThreadProcessId(IntPtr h,IntPtr unused);
 [DllImport("user32")] public static extern bool GetGUIThreadInfo(uint thread,ref GuiThread state);
 [DllImport("user32")] public static extern bool IsChild(IntPtr parent,IntPtr child);
 [DllImport("user32",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,System.Text.StringBuilder buffer,int length);
 [DllImport("user32",CharSet=CharSet.Unicode)] public static extern IntPtr SendMessage(IntPtr h,uint msg,IntPtr wp,string value);
 [DllImport("user32")] public static extern IntPtr WindowFromPoint(Point pt);
 [DllImport("user32")] public static extern IntPtr GetAncestor(IntPtr hwnd,uint flags);
 [DllImport("user32")] public static extern bool ScreenToClient(IntPtr hwnd,ref Point pt);
 [DllImport("user32")] public static extern bool ClientToScreen(IntPtr h,ref Point pt);
 [DllImport("user32")] public static extern uint GetDpiForWindow(IntPtr hwnd);
 [DllImport("user32")] public static extern IntPtr GetLastActivePopup(IntPtr owner);
 [DllImport("user32")] public static extern bool GetClientRect(IntPtr h,out Rect rect);
 [DllImport("user32")] public static extern bool GetWindowRect(IntPtr h,out Rect rect);
 [DllImport("user32")] public static extern bool IsWindowEnabled(IntPtr h);
 [DllImport("user32",EntryPoint="SendMessageW")] public static extern IntPtr HitTest(IntPtr h,uint msg,IntPtr wp,IntPtr lp);
 [DllImport("user32")] public static extern bool PostMessage(IntPtr hwnd,uint message,IntPtr wParam,IntPtr lParam);
 [DllImport("user32")] public static extern bool SetForegroundWindow(IntPtr hwnd);
 [DllImport("user32")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32")] public static extern bool SetWindowPos(IntPtr hwnd,IntPtr after,int x,int y,int w,int h,uint flags);
 [DllImport("user32",SetLastError=true)] public static extern bool PostThreadMessage(uint thread,uint message,IntPtr wp,IntPtr lp);
 [DllImport("user32")] public static extern void mouse_event(uint flags,uint x,uint y,uint data,UIntPtr extra);
 [DllImport("user32")] public static extern bool MoveWindow(IntPtr h,int x,int y,int width,int height,bool redraw);
 [DllImport("user32")] public static extern bool PrintWindow(IntPtr h,IntPtr dc,uint flags);
 [DllImport("user32")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
}
'@
$null=[NimboWindowQA]::SetThreadDpiAwarenessContext([intptr](-4))
$data=Join-Path $Fixture $(if($Switcher){'qt-multi'}elseif($Fresh){'qt-fresh'}else{'qt-data'})
$null=New-Item -Path $data -ItemType Directory -Force
if(!$Fresh){
$plain=[IO.File]::ReadAllBytes((Join-Path $Fixture $(if($Switcher){'multi-profile.json'}else{'test-profile.json'})))
[IO.File]::WriteAllBytes((Join-Path $data 'profile.dpapi'),[Security.Cryptography.ProtectedData]::Protect($plain,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser))
}
if($env:NIMBO_NATIVE_TEST_URL){[IO.File]::WriteAllText((Join-Path $data 'desktop.ini'),("[General]"+[Environment]::NewLine+"lastServer="+$env:NIMBO_NATIVE_TEST_URL))}
if($VoiceRejoin){$env:NIMBO_QA_PLAY_CUES='1'}
$env:QT_ACCESSIBILITY='1'
$env:QT_SCALE_FACTOR_ROUNDING_POLICY='PassThrough'
$clientArguments=@('--data-dir',('"'+$data+'"'))
if(!$env:NIMBO_QA_CAPTURE_CYCLES){$clientArguments+='--synthetic-media'}
$profiler=$null
if($env:NIMBO_QA_HEOB){
  $logPath=Join-Path $OutputDir 'heap-stacks.txt'
  $minimumTrace=if($env:NIMBO_QA_RETENTION_ROUTE){'-z4096'}else{'-z32768'}
  $profiler=Start-Process -FilePath $env:NIMBO_QA_HEOB -WorkingDirectory $OutputDir -ArgumentList (@('-p0','-d4','-k2','-G1','-l1',$minimumTrace,'-oheap-stacks.txt',('"'+$Client+'"'))+$clientArguments) -WindowStyle Hidden -PassThru
  $p=$null
  for($i=0;$i -lt 60 -and !$p -and !$profiler.HasExited;$i++){
    $child=Get-CimInstance Win32_Process -Filter ("ParentProcessId="+$profiler.Id) | Where-Object ExecutablePath -eq $Client | Select-Object -First 1
    if($child){$p=Get-Process -Id $child.ProcessId}else{Start-Sleep -Milliseconds 100}
  }
  if(!$p){if(!$profiler.HasExited){$profiler.Kill($true)};throw 'Profiler did not start the disposable client'}
}else{$p=Start-Process -FilePath $Client -ArgumentList $clientArguments -PassThru -WindowStyle Normal}
$traceStarted=$false
$heapEnabled=$false
$traceSession="NimboCaptureHeap-$($p.Id)"
try{
  if($env:NIMBO_QA_WPR_HEAP){
    & wpr.exe -start HeapSnapshot -filemode -instancename $traceSession
    if($LASTEXITCODE){throw "WPR could not start the allocation trace (exit $LASTEXITCODE)"}
    $traceStarted=$true
    & wpr.exe -snapshotconfig heap -pid $p.Id enable
    if($LASTEXITCODE){throw "WPR could not enable heap snapshots for test PID $($p.Id)"}
    $heapEnabled=$true
  }
  if($env:NIMBO_QA_LARGE_CHAT){$p.Id | Set-Content -LiteralPath (Join-Path $Fixture 'client.pid')}
  for($i=0;$i -lt 70 -and $p.MainWindowHandle -eq [IntPtr]::Zero;$i++){Start-Sleep -Milliseconds 100;$p.Refresh()}
  $hw=$p.MainWindowHandle;if($hw -eq [IntPtr]::Zero){throw 'Test EXE has no window'}
  $root=[Windows.Automation.AutomationElement]::FromHandle($hw)
  function Resize-Client([int]$width){
    $a=New-Object NimboWindowQA+Rect;$b=New-Object NimboWindowQA+Rect
    $null=[NimboWindowQA]::GetClientRect($hw,[ref]$a);$null=[NimboWindowQA]::GetWindowRect($hw,[ref]$b)
    $screens=[Windows.Forms.Screen]::AllScreens
    $right=($screens|ForEach-Object {$_.WorkingArea.Right}|Measure-Object -Maximum).Maximum
    $left=[Math]::Min(1280,[Math]::Max(0,$right-$width-18))
    $null=[NimboWindowQA]::MoveWindow($hw,$left,40,($width+$b.right-$b.x-$a.right),(784+$b.bottom-$b.y-$a.bottom),$true)
  }
  function Find-Control([string]$name,[Windows.Automation.ControlType]$type=$null){
    $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$name)
    if($type){$condition=[Windows.Automation.AndCondition]::new($condition,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,$type))}
    for($i=0;$i -lt 100;$i++){
      $root=[Windows.Automation.AutomationElement]::FromHandle($hw)
      foreach($el in $root.FindAll([Windows.Automation.TreeScope]::Descendants,$condition)){
        $rect=$el.Current.BoundingRectangle
        if(!$el.Current.IsOffscreen -and !$rect.IsEmpty -and $rect.Width -gt 0 -and $rect.Height -gt 0){return $el}
      }
      Start-Sleep -Milliseconds 100
    }
    Shot "missing"; $names=$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition) | ForEach-Object {$_.Current.Name} | Where-Object {$_} | Select-Object -First 25;throw "Missing control $name. Found $($names -join ', ')"
  }
  function Press([string]$name){$e=Find-Control $name;$e.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke();Start-Sleep -Milliseconds 250}
  function Test-ChannelIcons {
    Press 'Изменить общий';$null=Find-Control 'Название канала общий';Shot 'channel-edit'
    Press 'Выбрать иконку канала'
    $null=Find-Control 'Кофе'
    Shot 'channel-icons'
    $coffee=Find-Control 'Кофе'
    if($coffee.Current.BoundingRectangle.IsEmpty -or $coffee.Current.BoundingRectangle.Width -le 0){throw 'Coffee icon is not visible'}
    $r=$coffee.Current.BoundingRectangle
    $point=New-Object NimboWindowQA+Point
    $point.x=[int]($r.Left+$r.Width/2);$point.y=[int]($r.Top+$r.Height/2)
    $null=[NimboWindowQA]::ScreenToClient($hw,[ref]$point)
    $position=[IntPtr](($point.y -shl 16) -bor ($point.x -band 0xffff))
    $null=[NimboWindowQA]::PostMessage($hw,0x201,[IntPtr]1,$position)
    $null=[NimboWindowQA]::PostMessage($hw,0x202,[IntPtr]0,$position)
    for($i=0;$i -lt 40 -and !$coffee.Current.BoundingRectangle.IsEmpty -and !$coffee.Current.IsOffscreen;$i++){Start-Sleep -Milliseconds 50}

    Press 'Выбрать иконку канала'
    $selected=Find-Control 'Кофе'
    $pattern=$null
    if($selected.TryGetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern,[ref]$pattern)){
      if(!$pattern.Current.IsSelected){throw 'Coffee icon is not selected'}
    }elseif($selected.TryGetCurrentPattern([Windows.Automation.TogglePattern]::Pattern,[ref]$pattern)){
      if($pattern.Current.ToggleState -ne [Windows.Automation.ToggleState]::On){throw 'Coffee icon is not checked'}
    }else{throw "Cannot inspect selection on $($selected.Current.ControlType.ProgrammaticName)"}
    Shot 'channel-icon-selected'
    Press 'Выбрать иконку канала'
    Press 'Кто может писать'
    $everyone=Find-Control 'Все участники' ([Windows.Automation.ControlType]::CheckBox)
    $toggle=$everyone.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)
    if($toggle.Current.ToggleState -ne [Windows.Automation.ToggleState]::On){throw 'Initial policy is not everyone'}
    Shot 'channel-posting';$toggle.Toggle()
    if($toggle.Current.ToggleState -ne [Windows.Automation.ToggleState]::Off){throw 'Everyone option did not change'}
    Shot 'channel-posting-owner'
    Press 'Отменить изменения канала'
    Press 'Изменить общий';Press 'Кто может писать'
    $everyone=Find-Control 'Все участники' ([Windows.Automation.ControlType]::CheckBox)
    if($everyone.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -ne [Windows.Automation.ToggleState]::On){throw 'Cancelled posting policy persisted'}
    Press 'Отменить изменения канала' 
  }
  function Capture([string]$name){
    $r=New-Object NimboWindowQA+Rect;$null=[NimboWindowQA]::GetClientRect($hw,[ref]$r)
    $bitmap=New-Object Drawing.Bitmap($r.right,$r.bottom);$g=[Drawing.Graphics]::FromImage($bitmap);$dc=$g.GetHdc()
    try{if(![NimboWindowQA]::PrintWindow($hw,$dc,3)){throw 'Unable to capture test EXE'}}finally{$g.ReleaseHdc($dc);$g.Dispose()}
    try{
      if($name -in @('language-open','noise-open')){
        $popups=[Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$p.Id))
        $popup=$popups | Where-Object {$_.Current.NativeWindowHandle -and $_.Current.NativeWindowHandle -ne $hw.ToInt64() -and !$_.Current.IsOffscreen} | Select-Object -First 1
        if(!$popup){throw 'Language list did not open'}
        $handle=[IntPtr]$popup.Current.NativeWindowHandle;$r=New-Object NimboWindowQA+Rect;$null=[NimboWindowQA]::GetWindowRect($handle,[ref]$r)
        $origin=New-Object NimboWindowQA+Point;$null=[NimboWindowQA]::ClientToScreen($hw,[ref]$origin)
        $layer=[Drawing.Bitmap]::new($r.right-$r.x,$r.bottom-$r.y);$gr=[Drawing.Graphics]::FromImage($layer);$ldc=$gr.GetHdc()
        try{$null=[NimboWindowQA]::PrintWindow($handle,$ldc,2)}finally{$gr.ReleaseHdc($ldc);$gr.Dispose()}
        $gr=[Drawing.Graphics]::FromImage($bitmap);try{$gr.DrawImageUnscaled($layer,$r.x-$origin.x,$r.y-$origin.y)}finally{$gr.Dispose();$layer.Dispose()}
      }
      $bitmap.Save((Join-Path $OutputDir ("qt-"+$name+".png")),[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
  }
  function Shot([string]$name){
    if($name -ne 'missing'){
      for($i=0;$i -lt 350 -and !(Test-Path -LiteralPath (Join-Path $Fixture ("web-"+$name)));$i++){Start-Sleep -Milliseconds 100}
      if(!(Test-Path -LiteralPath (Join-Path $Fixture ("web-"+$name)))){throw "Browser did not reach $name"}
    }
    Start-Sleep -Milliseconds 350;Capture $name
    $focus=[Windows.Automation.AutomationElement]::FocusedElement
    $owned=$focus -and ($focus.Current.ProcessId -eq $p.Id)
    $record=[ordered]@{screen=$name;focusInTestWindow=[bool]$owned}
    if($owned){$record.control=$focus.Current.Name;$record.type=$focus.Current.ControlType.ProgrammaticName}
    Add-Content -LiteralPath (Join-Path $OutputDir 'qt-interaction.jsonl') -Value ($record | ConvertTo-Json -Compress) -Encoding utf8
    if($name -ne 'missing'){[IO.File]::WriteAllText((Join-Path $Fixture ("qt-"+$name)),'')}
  }
  function Click-Client([int]$x,[int]$y) {
    $pt=New-Object NimboWindowQA+Point;$pt.x=$x;$pt.y=$y;$null=[NimboWindowQA]::ClientToScreen($hw,[ref]$pt)
    [Windows.Forms.Cursor]::Position=[Drawing.Point]::new($pt.x,$pt.y)
    [NimboWindowQA]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[NimboWindowQA]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
  }
  Resize-Client 1264
  Start-Sleep -Milliseconds 200
  if(!$IdleMemory -and [NimboWindowQA]::GetDpiForWindow($hw) -ne 96){throw 'The native viewport must be at 100% to compare with the reference'}
  if($SavedLogin){
    function Open-ImportFixture {
      $button=Find-Control 'Войти через файл .nimbo'
      # UIA omits a clickable point when the reference browser overlaps the button.
      # The messages below are addressed to our test window and use its client coordinates.
      $bounds=$button.Current.BoundingRectangle
      if($bounds.IsEmpty -or $bounds.Width -lt 1 -or $bounds.Height -lt 1){throw 'Import button has no visible bounds'}
      $pt=New-Object NimboWindowQA+Point
      $pt.x=[int]($bounds.Left+$bounds.Width/2);$pt.y=[int]($bounds.Top+$bounds.Height/2)
      $null=[NimboWindowQA]::ScreenToClient($hw,[ref]$pt)
      $coords=[IntPtr](($pt.y -shl 16) -bor $pt.x)
      $null=[NimboWindowQA]::PostMessage($hw,513,[IntPtr]1,$coords);$null=[NimboWindowQA]::PostMessage($hw,514,[IntPtr]0,$coords)
      $dialogCondition=[Windows.Automation.AndCondition]::new(
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ClassNameProperty,'#32770'),
        [Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$p.Id))
      $dialog=$null
      for($i=0;$i -lt 60 -and !$dialog;$i++){
        $dialog=$root.FindFirst([Windows.Automation.TreeScope]::Descendants,$dialogCondition)
        if(!$dialog){$dialog=[Windows.Automation.AutomationElement]::RootElement.FindFirst([Windows.Automation.TreeScope]::Children,$dialogCondition)}
        Start-Sleep -Milliseconds 80
      }
      if(!$dialog){throw 'Native import file picker did not open'}
      $scope=[Windows.Automation.TreeScope]::Descendants
      $handle=[IntPtr]$dialog.Current.NativeWindowHandle
      $thread=[NimboWindowQA]::GetWindowThreadProcessId($handle,[IntPtr]::Zero)
      $focus=[IntPtr]::Zero
      for($i=0;$i -lt 50;$i++){
        $gui=New-Object NimboWindowQA+GuiThread
        $gui.size=[Runtime.InteropServices.Marshal]::SizeOf($gui)
        $null=[NimboWindowQA]::GetGUIThreadInfo($thread,[ref]$gui)
        $class=[Text.StringBuilder]::new(128)
        $null=[NimboWindowQA]::GetClassName($gui.focus,$class,128)
        if(([NimboWindowQA]::IsChild($handle,$gui.focus)) -and $class.ToString() -eq 'Edit'){$focus=$gui.focus;break}
        Start-Sleep -Milliseconds 100
      }
      if(!$focus.ToInt64()){throw "Initial file name field did not receive focus in the test dialog ($class)"}
      $null=[NimboWindowQA]::SendMessage($focus,12,[IntPtr]0,(Join-Path $Fixture 'import-test.nimbo'))
      $null=[NimboWindowQA]::PostMessage($handle,273,[IntPtr]1,[IntPtr]0)
      $null=Find-Control 'Пароль файла' ([Windows.Automation.ControlType]::Edit)
    }
    Open-ImportFixture;Shot 'login-protected';Press 'Отмена';Open-ImportFixture
    function Unlock-Import([string]$value){
      $passwordField=Find-Control 'Пароль файла' ([Windows.Automation.ControlType]::Edit)
      $passwordField.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue($value)
      Press 'Открыть профиль'
    }
    Unlock-Import 'invalid-password';$null=Find-Control 'Неверный пароль или файл повреждён.';Shot 'login-password-error'
    Unlock-Import 'native-fixture';$null=Find-Control 'Имя профиля';Shot 'login-home' 
    Press 'Выйти из профиля';$null=Find-Control 'Сохранённый профиль';Shot 'login-saved';return
  }
  if($Fresh){
    Shot 'login';return
  }
  $initialServer=if($env:NIMBO_QA_CHAT_CALL_CLOSE){$env:NIMBO_NATIVE_TEST_URL2}else{$env:NIMBO_NATIVE_TEST_URL}
  Press ('Открыть сервер '+([uri]$initialServer).Authority)
  $null=Find-Control 'Настройки'
  if($IdleMemory){
    $csv=Join-Path $OutputDir 'idle-memory.csv'
    function Sample-Idle([string]$phase,[int[]]$seconds){
      $clock=[Diagnostics.Stopwatch]::StartNew()
      foreach($s in $seconds){
        while($clock.Elapsed.TotalSeconds -lt $s){Start-Sleep -Milliseconds 250}
        $p.Refresh()
        [pscustomobject]@{phase=$phase;seconds=$s;pid=$p.Id;workingBytes=$p.WorkingSet64;privateBytes=$p.PrivateMemorySize64;threads=$p.Threads.Count;handles=$p.HandleCount} |
          Export-Csv -NoTypeInformation -Append -LiteralPath $csv
        if($s -eq 60 -and $phase -eq 'media'){
          Get-CimInstance Win32_PerfRawData_PerfProc_Process -Filter ("IDProcess="+$p.Id) |
            Select-Object IDProcess,WorkingSet,WorkingSetPrivate,PrivateBytes,ThreadCount |
            ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDir 'private-working-set.json')
        }
      }
    }
    if($env:NIMBO_QA_AUDIO_PHOTO){
      function Click-TestControl($el,[int]$dx=-1,[int]$dy=-1,[IntPtr]$target=$hw){
        $r=$el.Current.BoundingRectangle
        $pt=New-Object NimboWindowQA+Point
        $pt.x=if($dx -lt 0){[int]($r.Left+$r.Width/2)}else{[int]$r.Left+$dx}
        $pt.y=if($dy -lt 0){[int]($r.Top+$r.Height/2)}else{[int]$r.Top+$dy}
        $null=[NimboWindowQA]::ScreenToClient($target,[ref]$pt)
        $pos=[IntPtr](($pt.y -shl 16) -bor ($pt.x -band 0xffff))
        $null=[NimboWindowQA]::PostMessage($target,0x201,[IntPtr]1,$pos)
        $null=[NimboWindowQA]::PostMessage($target,0x202,[IntPtr]0,$pos)
      }
      function Select-TestRoom([string]$name){
        Click-TestControl (Find-Control $name ([Windows.Automation.ControlType]::ListItem))
        Start-Sleep -Milliseconds 400
      }
      function Leave-TestCall {
        $root=[Windows.Automation.AutomationElement]::FromHandle($hw)
        # Both the hovering tile action and the permanent bottom bar expose
        # the same accessible name. Select the visible full-size bottom button.
        $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Выйти из голосовой комнаты')
        $button=$root.FindAll([Windows.Automation.TreeScope]::Descendants,$condition) |
          Where-Object {!$_.Current.IsOffscreen -and $_.Current.BoundingRectangle.Width -gt 50} |
          Sort-Object {$_.Current.BoundingRectangle.Bottom} -Descending | Select-Object -First 1
        if(!$button){throw 'Call bar has no visible leave button'}
        Click-TestControl $button
        $null=Find-Control 'Присоединяйтесь к разговору.'
      }
      function Measure-Stage([string]$label){
        Sample-Idle $label @(1)
        if ($env:NIMBO_QA_VMMAP -and $label -in @('idle','rest-final')) {
          $mapFile=Join-Path $OutputDir ($label+'-vmmap.csv')
          for($attempt=0;$attempt -lt 3;$attempt++){
            $mapTool=Start-Process -FilePath $env:NIMBO_QA_VMMAP -ArgumentList @('-accepteula','-p',$p.Id,('"'+$mapFile+'"')) -WindowStyle Hidden -PassThru
            if(!$mapTool.WaitForExit(25000)){ $mapTool.Kill();throw 'VMMap timed out' }
            if(!(Test-Path -LiteralPath $mapFile)){throw 'Missing VMMap report'}
            # A changing address space can produce a truncated VMMap enumeration.
            if(Select-String -LiteralPath $mapFile -Pattern '^"Image","[0-9]' -Quiet){break}
            Start-Sleep -Milliseconds 250
          }
        }
        if($heapEnabled -and $label -in @('voice','both-on-1','camera-on-1','screen-on-1','capture-rest','left')){
          & wpr.exe -singlesnapshot heap $p.Id -instancename $traceSession
          if($LASTEXITCODE){throw "Could not capture the heap at $label"}
          [pscustomobject]@{phase=$label;pid=$p.Id;utc=[DateTime]::UtcNow.ToString('o')} |
            Export-Csv -Append -NoTypeInformation -LiteralPath (Join-Path $OutputDir 'wpr-phases.csv')
        }
        if($profiler -and $label -in ($env:NIMBO_QA_HEAP_STAGES -split ',')){
          $profiler.Refresh()
          $thread=$profiler.Threads | Sort-Object StartTime | Select-Object -First 1
          $offset=(Get-Item -LiteralPath $logPath).Length
          if(![NimboWindowQA]::PostThreadMessage($thread.Id,0x0312,[IntPtr]3,[IntPtr]0)){throw 'Profiler could not receive snapshot request'}
          # SHOW reports allocations made since the previous snapshot and still live.
          # A pause between two stack writes does not mark the end of the snapshot.
          for($i=0;$i -lt 100;$i++){
            Start-Sleep -Milliseconds 100
            $length=(Get-Item -LiteralPath $logPath).Length
            if($length -gt $offset -and (Get-Content -LiteralPath $logPath -Tail 1) -match '^  sum:|^no leaks'){break}
          }
          if($i -eq 100){throw "Incomplete heap snapshot at $label"}
          [pscustomobject]@{phase=$label;start=$offset;end=$length} | Export-Csv -Append -NoTypeInformation -LiteralPath (Join-Path $OutputDir 'heap-phases.csv')
        }
        if($env:NIMBO_QA_CHAT_CALL_CLOSE -or $env:NIMBO_QA_VERIFY_PLAYBACK -or $env:NIMBO_QA_CAPTURE_COUNTERS){
          $stats=@(Get-CimInstance Win32_PerfRawData_PerfProc_Process -Filter ("IDProcess="+$p.Id+" OR CreatingProcessID="+$p.Id))
          $stats | Select-Object @{n='phase';e={$label}},Name,IDProcess,WorkingSetPrivate,PrivateBytes |
            Export-Csv -Append -NoTypeInformation -LiteralPath (Join-Path $OutputDir 'stage-processes.csv')
          if(@($stats | Where-Object IDProcess -ne $p.Id).Count){throw "Unexpected client child process at $label"}
          if([NimboWindowQA]::HitTest($hw,0x80db,[IntPtr]::Zero,[IntPtr]::Zero) -ne [IntPtr]1){throw 'Missing client resource sample'}
          $resources=Get-Content -Raw -LiteralPath (Join-Path $data 'qa-memory.json') | ConvertFrom-Json
          $resources | Select-Object @{n='phase';e={$label}},* | Export-Csv -NoTypeInformation -Append -LiteralPath (Join-Path $OutputDir 'resources.csv')
          if($env:NIMBO_QA_GAME_OVERLAY){
            if($label -like 'overlay-visible-*' -and !$resources.gameOverlay){throw "Overlay missing at $label"}
            if(($label -like 'overlay-hidden-*' -or $label -like 'left-*' -or $label -eq 'rest-final') -and $resources.gameOverlay){throw "Overlay survived $label"}
          }
          if($label -like 'settings-closed-*' -and ($resources.settingsPages -or $resources.effectCards -or $resources.audioChecks)){
            throw "Settings resources survived the return to chat at $label"
          }
          if(($label -like '*-on-*' -or $label -like 'screen-chat-*') -and $label -notlike 'control-*' -and !$resources.videoFrames){throw "No frame delivered at $label"}
          if($label -like '*-off-*' -and (!$resources.voiceClient -or $resources.videoFrames)){throw "Unexpected camera/screen or call state at $label"}
          if($env:NIMBO_QA_VERIFY_PLAYBACK){
            if($label -like 'play-*' -and $resources.playingAttachments -ne 1){throw 'Missing active attachment player'}
            if($label -like 'pause-*' -and $resources.attachmentDecoders){throw 'Attachment decoder survived pause'}
          }
          if($env:NIMBO_QA_CHAT_CALL_CLOSE -and ($label -like 'left-*' -or $label -eq 'rest-final')){
            foreach($key in @('rows','messages','thumbnailKiB','audioCards','videoCards','downloads','videoFrames','sounds','voiceClient')){
              if($resources.$key){throw "$key retained after $label"}
            }
          }
        }
        $mapPause=$env:NIMBO_QA_PAUSE_REST -and $label -in @('history-1','play-1','pause-1','pause-rest')
        $mapSettings=$env:NIMBO_QA_SETTINGS_CYCLES -and $label -in @('settings-video-1','settings-closed-1','settings-closed-3')
        $mapCaptureStop=$env:NIMBO_QA_CAPTURE_COUNTERS -and $label -eq 'screen-off-5'
        if(($mapSettings -or $mapPause -or $mapCaptureStop -or $label -in @('idle','rest-final','workspace-final','voice','capture-rest','left')) -and $env:NIMBO_QA_PYTHON){
          & $env:NIMBO_QA_PYTHON (Join-Path $PSScriptRoot 'measure-native-idle.py') --map-pid $p.Id |
            Set-Content -LiteralPath (Join-Path $OutputDir ($label+'-map.json'))
          if($LASTEXITCODE){throw 'Could not classify resident pages'}
        }
        Get-CimInstance Win32_PerfRawData_PerfProc_Process -Filter ("IDProcess="+$p.Id) |
          Select-Object @{n='phase';e={$label}},WorkingSetPrivate,WorkingSet,PrivateBytes,ThreadCount |
          Export-Csv -NoTypeInformation -Append -LiteralPath (Join-Path $OutputDir 'media-private.csv')
      }
        function MediaRow([string]$title){
          $match=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$title)
          # Stack instrumentation slows initial chat layout. Still require the
          # real, laid-out row before clicking or taking a measurement.
          $attempts=if($profiler){200}else{50}
          for($n=0;$n -lt $attempts;$n++){
            $root=[Windows.Automation.AutomationElement]::FromHandle($hw)
            $row=$root.FindAll([Windows.Automation.TreeScope]::Descendants,$match) |
              Where-Object {$_.Current.ControlType -eq [Windows.Automation.ControlType]::ListItem -and $_.Current.BoundingRectangle.Width -gt 280} | Select-Object -First 1
            if($row){return $row}
            Start-Sleep -Milliseconds 100
          }
          $listItems=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem)
          $names=$root.FindAll([Windows.Automation.TreeScope]::Descendants,$listItems) | ForEach-Object {$_.Current.Name} | Select-Object -Last 15
          throw "Missing message row $title. Visible item tree ends with: $($names -join ', ')"
        }
      function Reveal-RecentRow([string]$title,[int]$dy){
        for($step=0;$step -lt 150;$step++){
          $row=MediaRow $title
          $y=$row.Current.BoundingRectangle.Top+$dy
          if(!$row.Current.IsOffscreen -and $y -gt $bounds.Top+110 -and $y -lt $bounds.Bottom-85){return $row}
          $wheel=if($y -gt $bounds.Bottom-85){-120}else{120}
          $null=[NimboWindowQA]::PostMessage($hw,0x20A,[IntPtr]($wheel -shl 16),$point)
          Start-Sleep -Milliseconds 150
        }
        throw "Could not scroll to $title"
      }
      if($env:NIMBO_QA_PLAYER_SMOKE){
        # Sample only the fixture's visible video region on the Windows desktop.
        # PrintWindow can miss child HWNDs and compositor video surfaces.
        function Read-VideoFrame($row,[string]$name){
          $r=$row.Current.BoundingRectangle
          $x=[int]$r.Left+62; $y=[int]$r.Top+78
          $pt=New-Object NimboWindowQA+Point; $pt.x=$x+160; $pt.y=$y+60
          $hit=[NimboWindowQA]::WindowFromPoint($pt)
          if([NimboWindowQA]::GetAncestor($hit,2) -ne $hw){throw "Video fixture is covered: $name"}
          $bitmap=[Drawing.Bitmap]::new(390,230)
          $g=[Drawing.Graphics]::FromImage($bitmap)
          try{
            $g.CopyFromScreen($x,$y,0,0,$bitmap.Size)
            $bitmap.Save((Join-Path $OutputDir ($name+'.png')),[Drawing.Imaging.ImageFormat]::Png)
          }finally{$g.Dispose();$bitmap.Dispose()}
        }
        $null=[NimboWindowQA]::SetForegroundWindow($hw)
        $null=[NimboWindowQA]::SetWindowPos($hw,[IntPtr](-1),0,0,0,0,0x13)
        function Select-PlayerRoom([string]$name){
          (Find-Control $name ([Windows.Automation.ControlType]::ListItem)).GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
        }
        function Decoder-State {
          if([NimboWindowQA]::HitTest($hw,0x80db,[IntPtr]::Zero,[IntPtr]::Zero) -ne [IntPtr]1){throw 'QA counters unavailable'}
          return (Get-Content -Raw -LiteralPath (Join-Path $data 'qa-memory.json') | ConvertFrom-Json)
        }
        Select-PlayerRoom 'общий'
        $row=Find-Control 'Клип' ([Windows.Automation.ControlType]::ListItem)
        foreach($want in @(1,0,1,0)){
          # The Qt list exposes its item via UIA, not the embedded buttons.
          # Exercise the visible Play control on the shipped 480x270 test clip.
          Click-TestControl $row 72 325
          for($i=0;$i -lt 80;$i++){
            $s=Decoder-State
            if($s.playingAttachments -eq $want -and $s.attachmentDecoders -eq $want){break}
            Start-Sleep -Milliseconds 100
          }
          if($i -eq 80){throw "MP4 failed to reach playing=$want; decoders=$($s.attachmentDecoders)"}
          Write-Output "Portable MP4: playing=$want decoders=$($s.attachmentDecoders)"
          if($want){
            Read-VideoFrame $row 'mp4-a'
            Start-Sleep -Milliseconds 480
            Read-VideoFrame $row 'mp4-b'
          }
          Start-Sleep -Milliseconds 200
        }
        Select-PlayerRoom 'YouTube'
        function Show-ClipRow([string]$link){
          $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$link)
          $bounds=$root.Current.BoundingRectangle
          for($step=0;$step -lt 80;$step++){
            $root=[Windows.Automation.AutomationElement]::FromHandle($hw)
            $row=$root.FindAll([Windows.Automation.TreeScope]::Descendants,$condition) |
              Where-Object {$_.Current.ControlType -eq [Windows.Automation.ControlType]::ListItem} | Select-Object -First 1
            if(!$row -and $step -lt 8){Start-Sleep -Milliseconds 150;continue}
            $delta=120
            if($row){
              $r=$row.Current.BoundingRectangle
              if(!$r.IsEmpty){
                if($r.Top+68 -gt $bounds.Top+110 -and $r.Top+312 -lt $bounds.Bottom-80){return $row}
                if($r.Top+312 -ge $bounds.Bottom-80){$delta=4294967176}
              }
            }
            $pt=New-Object NimboWindowQA+Point;$pt.x=[int]$bounds.Left+805;$pt.y=[int]$bounds.Top+320
            if([NimboWindowQA]::GetAncestor([NimboWindowQA]::WindowFromPoint($pt),2) -ne $hw){throw 'Chat covered during scroll'}
            $old=[Windows.Forms.Cursor]::Position
            try{
              [Windows.Forms.Cursor]::Position=[Drawing.Point]::new($pt.x,$pt.y)
              [NimboWindowQA]::mouse_event(0x800,0,0,$delta,[UIntPtr]::Zero)
              Start-Sleep -Milliseconds 60
            }finally{[Windows.Forms.Cursor]::Position=$old}
            Start-Sleep -Milliseconds 130
          }
          throw "Video row outside viewport: $link, bounds $r, window $bounds"
        }
        $row=Show-ClipRow $env:NIMBO_QA_PLAYER_LINK
        Click-TestControl $row 150 145
        for($i=0;$i -lt 350;$i++){
          if((Decoder-State).youtubePlaying){break}
          Start-Sleep -Milliseconds 100
        }
        if($i -eq 350){throw 'Portable YouTube did not play after one click'}
        Write-Output 'Portable YouTube: playing'
        Start-Sleep -Milliseconds 1900
        Read-VideoFrame $row 'youtube-a'
        Start-Sleep -Milliseconds 680
        Read-VideoFrame $row 'youtube-b'
        $r=$row.Current.BoundingRectangle
        # The mobile-sized embed places its pause button at the center of the picture.
        $pt=New-Object NimboWindowQA+Point; $pt.x=[int]$r.Left+262; $pt.y=[int]$r.Top+188
        $target=[NimboWindowQA]::WindowFromPoint($pt)
        $class=[Text.StringBuilder]::new(128);$null=[NimboWindowQA]::GetClassName($target,$class,128)
        Write-Output "Window under video: $class"
        Click-TestControl $row 262 188 $target
        for($i=0;$i -lt 60 -and (Decoder-State).youtubeSurfaces;$i++){Start-Sleep -Milliseconds 100}
        if((Decoder-State).youtubeSurfaces){throw 'A click on the visible YouTube frame did not pause playback'}
        Write-Output 'Portable YouTube: provider pause released surface'
        Click-TestControl $row 150 145
        for($i=0;$i -lt 350 -and !(Decoder-State).youtubePlaying;$i++){Start-Sleep -Milliseconds 100}
        if(!(Decoder-State).youtubePlaying){throw 'YouTube did not resume'}
        Start-Sleep -Milliseconds 1500
        Read-VideoFrame $row 'youtube-resumed'
        $second=Show-ClipRow $env:NIMBO_QA_SECOND_LINK
        Click-TestControl $second 150 145
        for($i=0;$i -lt 100;$i++){
          $r=$second.Current.BoundingRectangle
          $pt=New-Object NimboWindowQA+Point;$pt.x=[int]$r.Left+260;$pt.y=[int]$r.Top+188
          $class=[Text.StringBuilder]::new(128)
          $null=[NimboWindowQA]::GetClassName([NimboWindowQA]::WindowFromPoint($pt),$class,128)
          if($class.ToString() -eq 'Chrome_RenderWidgetHostHWND'){break}
          Start-Sleep -Milliseconds 100
        }
        Start-Sleep -Milliseconds 1000
        Read-VideoFrame $second 'youtube-second'
        # Exercise YouTube's own fullscreen button through its render HWND.
        function Current-WindowRect {
          $rect=New-Object NimboWindowQA+Rect
          $null=[NimboWindowQA]::GetWindowRect($hw,[ref]$rect)
          return [Drawing.Rectangle]::FromLTRB($rect.x,$rect.y,$rect.right,$rect.bottom)
        }
        $normal=Current-WindowRect
        $r=$second.Current.BoundingRectangle
        $pt=New-Object NimboWindowQA+Point;$pt.x=[int]$r.Left+260;$pt.y=[int]$r.Top+188
        $target=[NimboWindowQA]::WindowFromPoint($pt)
        $class=[Text.StringBuilder]::new(128);$null=[NimboWindowQA]::GetClassName($target,$class,128)
        if($class.ToString() -ne 'Chrome_RenderWidgetHostHWND'){throw "Second card has no player at its center: $class"}
        $monitor=[Windows.Forms.Screen]::FromHandle($hw).Bounds
        $old=[Windows.Forms.Cursor]::Position
        $pt.x=[int]$r.Left+424;$pt.y=[int]$r.Top+205
        $null=[NimboWindowQA]::SetForegroundWindow($hw)
        if([NimboWindowQA]::WindowFromPoint($pt) -ne $target){throw 'Fullscreen control outside the video'}
        try{
          [Windows.Forms.Cursor]::Position=[Drawing.Point]::new($pt.x,$pt.y)
          Start-Sleep -Milliseconds 220
          [NimboWindowQA]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[NimboWindowQA]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
        }finally{[Windows.Forms.Cursor]::Position=$old}
        for($i=0;$i -lt 40;$i++){
          $b=Current-WindowRect
          if([math]::Abs($b.Width-$monitor.Width) -lt 3 -and [math]::Abs($b.Height-$monitor.Height) -lt 3){break}
          Start-Sleep -Milliseconds 100
        }
        if($i -eq 40){throw "YouTube did not expand to monitor: $b, monitor $monitor"}
        foreach($fraction in @(0.1,0.5,0.9)){
          $pt.x=[int]($b.Left+$b.Width*$fraction);$pt.y=[int]($b.Top+$b.Height*$fraction)
          if([NimboWindowQA]::WindowFromPoint($pt) -ne $target){throw 'Only part of the fullscreen surface receives video input'}
        }
        Start-Sleep -Milliseconds 1400
        $large=[Drawing.Bitmap]::new($b.Width,$b.Height)
        $g=[Drawing.Graphics]::FromImage($large)
        try{
          $g.CopyFromScreen([int]$b.Left,[int]$b.Top,0,0,$large.Size)
          $small=[Drawing.Bitmap]::new($large,640,360)
          try{$small.Save((Join-Path $OutputDir 'youtube-full.png'),[Drawing.Imaging.ImageFormat]::Png)}finally{$small.Dispose()}
        }finally{$g.Dispose();$large.Dispose()}
        if([NimboWindowQA]::GetForegroundWindow() -ne $hw){throw 'Test player lost focus before Escape'}
        [Windows.Forms.SendKeys]::SendWait('{ESC}')
        for($i=0;$i -lt 40 -and (Current-WindowRect) -ne $normal;$i++){Start-Sleep -Milliseconds 100}
        if((Current-WindowRect) -ne $normal){throw "YouTube did not restore the chat window: $(Current-WindowRect), expected $normal"}
        Write-Output "Second video expanded to $($monitor.Width)x$($monitor.Height) and restored the chat"
        $row=Show-ClipRow $env:NIMBO_QA_PLAYER_LINK
        Start-Sleep -Milliseconds 800
        Read-VideoFrame $row 'youtube-return'
        Select-PlayerRoom 'общий'
        for($i=0;$i -lt 100 -and (Decoder-State).youtubeSurfaces;$i++){Start-Sleep -Milliseconds 100}
        if((Decoder-State).youtubeSurfaces){throw 'YouTube survived channel departure'}
        return
      }
      function Start-TestScreen {
        Click-TestControl (Find-Control 'Показать экран')
        for($i=0;$i -lt 50;$i++){
          $popup=[NimboWindowQA]::GetLastActivePopup($hw)
          if($popup -ne $hw -and $popup -ne [IntPtr]::Zero){break}
          Start-Sleep -Milliseconds 100
        }
        $dialog=[Windows.Automation.AutomationElement]::FromHandle($popup)
        for($i=0;$i -lt 30;$i++){
          $elements=$dialog.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
          $monitor=$elements | Where-Object {$_.Current.Name -like 'Монитор 1*'} | Select-Object -First 1
          $confirm=$elements | Where-Object {$_.Current.Name -eq 'Поделиться'} | Select-Object -First 1
          if($monitor -and $confirm){break}
          Start-Sleep -Milliseconds 100
        }
        if(!$monitor -or !$confirm){throw "Monitor or share button missing in $($dialog.Current.Name)"}
        Click-TestControl $monitor -target $popup
        Start-Sleep -Milliseconds 150
        $confirm.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
        $null=Find-Control 'Остановить показ экрана'
      }
      Select-TestRoom 'общий';Measure-Stage 'idle'
      if($env:NIMBO_QA_LANGUAGE_CYCLES){
        Press 'Настройки'
        Measure-Stage 'general-before'
        $targets=@(@('Язык','English','Back to room','en'),@('Language','Русский','К комнате','ru'))
        if($env:NIMBO_QA_LANGUAGE_CONTROL){$targets=@(@('Язык','Русский','К комнате','control-a'),@('Язык','Русский','К комнате','control-b'))}
        foreach($round in 1..10){
          foreach($target in $targets){
            $box=Find-Control $target[0] ([Windows.Automation.ControlType]::ComboBox)
            $expand=$box.GetCurrentPattern([Windows.Automation.ExpandCollapsePattern]::Pattern)
            $expand.Expand()
            $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$p.Id)
            $option=$null
            $name=$target[1]
            $match=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$name)
            for($n=0;$n -lt 80 -and !$option;$n++){
              if($n -in @(20,40,60) -and $expand.Current.ExpandCollapseState -eq [Windows.Automation.ExpandCollapseState]::Collapsed){$expand.Expand()}
              foreach($popup in [Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children,$condition)){
                if(!$popup.Current.NativeWindowHandle -or $popup.Current.NativeWindowHandle -eq $hw.ToInt64()){continue}
                $option=$popup.FindFirst([Windows.Automation.TreeScope]::Descendants,$match)
                if($option){Click-TestControl $option -target ([IntPtr]$popup.Current.NativeWindowHandle);break}
              }
              Start-Sleep -Milliseconds 50
            }
            if(!$option){throw "Language option missing: $name"}
            $null=Find-Control $target[2]
            Measure-Stage ($target[3]+'-'+$round)
            if($round -eq 1 -and !$env:NIMBO_QA_LANGUAGE_CONTROL){Capture ('general-'+$target[3])}
          }
        }
        Press 'К комнате'
        Measure-Stage 'chat-after'
        $samples=Import-Csv -LiteralPath (Join-Path $OutputDir 'resources.csv')
        $first=$samples | Where-Object phase -in @('ru-2','control-b-2') | Select-Object -First 1
        $last=$samples | Where-Object phase -in @('ru-10','control-b-10') | Select-Object -First 1
        $closed=$samples | Where-Object phase -eq 'chat-after'
        if(!$first -or !$last -or [int]$last.widgets -gt [int]$first.widgets -or [int]$closed.settingsPages -ne 0){
          throw 'Settings widgets accumulated during language selection'
        }
        if(!$p.CloseMainWindow() -or !$p.WaitForExit(8000) -or $p.ExitCode -ne 0){throw 'Client failed to close after language verification'}
        return
      }
      if($env:NIMBO_QA_SETTINGS_CYCLES){
        foreach($round in 1..3){
          Press 'Настройки';Measure-Stage "general-open-$round"
          foreach($tab in @(@('Звук','audio'),@('Видео','video'),@('Каналы','channels'),@('Участники и роли','roles'),@('Горячие клавиши','hotkeys'))){
            Press $tab[0]
            Measure-Stage ("settings-"+$tab[1]+"-"+$round)
          }
          Press 'К комнате';Measure-Stage "settings-closed-$round"
        }
        if($env:NIMBO_QA_SETTINGS_ONLY){
          $null=$p.CloseMainWindow()
          if(!$p.WaitForExit(8000)){throw 'Client did not exit after settings measurement'}
          if($profiler){
            if(!$profiler.WaitForExit(8000) -or !(Select-String -LiteralPath $logPath -Pattern '^exit code: 0 \(' -Quiet)){throw 'Profiler did not confirm normal client exit'}
          }elseif($p.ExitCode -ne 0){throw "Client exited with code $($p.ExitCode)"}
          return
        }
      }
      if($env:NIMBO_QA_CAPTURE_CYCLES){
        $mode=$env:NIMBO_QA_CAPTURE_CYCLES
        Select-TestRoom 'Гостиная'
        $null=Find-Control 'Выключить микрофон'
        Measure-Stage 'voice'
        foreach($cycle in 1..5){
          if($mode -in @('camera','both')){
            Press 'Включить камеру'
            $null=Find-Control 'Выключить камеру'
          }
          if($mode -in @('screen','both')){Start-TestScreen}
          $p.Refresh()
          $cpuStart=$p.TotalProcessorTime
          $interval=[Diagnostics.Stopwatch]::StartNew()
          Start-Sleep -Seconds 3
          $p.Refresh()
          [pscustomobject]@{mode=$mode;cycle=$cycle;cpuPercent=100*($p.TotalProcessorTime-$cpuStart).TotalSeconds/$interval.Elapsed.TotalSeconds/[Environment]::ProcessorCount} |
            Export-Csv -Append -NoTypeInformation -LiteralPath (Join-Path $OutputDir 'capture-cpu.csv')
          Measure-Stage "$mode-on-$cycle"
          if($mode -in @('camera','both')){
            Press 'Выключить камеру'
            $null=Find-Control 'Включить камеру'
          }
          if($mode -in @('screen','both')){
            Press 'Остановить показ экрана'
            $null=Find-Control 'Показать экран'
          }
          Start-Sleep -Seconds 2
          Measure-Stage "$mode-off-$cycle"
        }
        Start-Sleep -Seconds 30
        Measure-Stage 'capture-rest'
        Leave-TestCall
        Measure-Stage 'left'
        if($traceStarted){
          & wpr.exe -stop (Join-Path $OutputDir 'capture-heap.etl') -instancename $traceSession
          if($LASTEXITCODE){throw 'WPR could not save the heap trace'}
          $traceStarted=$false
        }
        return
      }
      if($env:NIMBO_QA_SHARE_PICKER){
        Select-TestRoom 'Гостиная'
        $null=Find-Control 'Выключить микрофон'
        Measure-Stage 'call'
        foreach($cycle in 1..4){
          if($env:NIMBO_QA_PICKER_CONTROL){
            foreach($phase in @('monitors','programs','closed')){Measure-Stage "$phase-$cycle"}
            continue
          }
          Click-TestControl (Find-Control 'Показать экран')
          $popup=[IntPtr]::Zero
          for($i=0;$i -lt 50;$i++){
            $popup=[NimboWindowQA]::GetLastActivePopup($hw)
            if($popup -ne $hw -and $popup -ne [IntPtr]::Zero){break}
            Start-Sleep -Milliseconds 100
          }
          if($popup -eq $hw -or $popup -eq [IntPtr]::Zero){throw 'Source picker did not open'}
          $dialog=[Windows.Automation.AutomationElement]::FromHandle($popup)
          function PickerButton([string]$name){
            $control=$dialog.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$name))
            if(!$control){throw "Source picker control missing: $name"}
            return $control
          }
          function Check-PickerCards([string]$tab){
            $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::AutomationIdProperty,'source')
            $cards=$dialog.FindAll([Windows.Automation.TreeScope]::Descendants,$condition)
            if(!$cards.Count){
              $elements=$dialog.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition)
              $cards=@($elements | Where-Object {
                $_.Current.Name -and !$_.Current.IsOffscreen -and $_.Current.BoundingRectangle.Width -gt 130 -and
                $_.Current.BoundingRectangle.Width -lt 260 -and $_.Current.BoundingRectangle.Height -gt 100
              })
            }
            if(!$cards.Count){throw "No visible cards for $tab"}
            $rect=$cards[0].Current.BoundingRectangle
            [pscustomobject]@{tab=$tab;count=$cards.Count;height=$rect.Height;width=$rect.Width} |
              Export-Csv -NoTypeInformation -Append -LiteralPath (Join-Path $OutputDir 'card-size.csv')
            $bounds=$dialog.Current.BoundingRectangle
            $image=[Drawing.Bitmap]::new([int]$bounds.Width,[int]$bounds.Height)
            $g=[Drawing.Graphics]::FromImage($image)
            try{
              $dc=$g.GetHdc()
              try{$null=[NimboWindowQA]::PrintWindow($popup,$dc,2)}finally{$g.ReleaseHdc($dc)}
              # Keep only layout evidence. Obscure monitor content and window titles before writing.
              $g.SetClip([Drawing.Rectangle]::new(0,110,$image.Width,$image.Height-185))
              foreach($c in $cards){
                $r=$c.Current.BoundingRectangle
                if($r.IsEmpty -or $r.Width -le 16 -or $r.Height -le 16){continue}
                $x=[int]($r.X-$bounds.X+8);$y=[int]($r.Y-$bounds.Y+8)
                $w=[int]$r.Width-16;$h=[Math]::Min(94,[int]$r.Height-16)
                $clip=[Drawing.Rectangle]::Intersect([Drawing.Rectangle]::new($x,$y,$w,$h),[Drawing.Rectangle]::new(0,110,$image.Width,$image.Height-185))
                if($clip.Width -gt 0 -and $clip.Height -gt 0){
                  $tiny=[Drawing.Bitmap]::new(8,4);$small=[Drawing.Graphics]::FromImage($tiny)
                  try{$small.DrawImage($image,[Drawing.Rectangle]::new(0,0,8,4),$clip,[Drawing.GraphicsUnit]::Pixel)}finally{$small.Dispose()}
                  try{$g.DrawImage($tiny,$clip)}finally{$tiny.Dispose()}
                }
                if($tab -eq 'programs'){$g.FillRectangle([Drawing.Brushes]::DimGray,$x,$y+$h+4,$w,16)}
              }
              $image.Save((Join-Path $OutputDir ("picker-"+$tab+'.png')))
            }finally{$g.Dispose();$image.Dispose()}
          }
          $null=PickerButton 'Мониторы'
          Measure-Stage "monitors-$cycle"
          if($cycle -eq 1){Check-PickerCards 'monitors'}
          (PickerButton 'Программы').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
          Measure-Stage "programs-$cycle"
          if($cycle -eq 1){Check-PickerCards 'programs'}
          (PickerButton 'Отмена').GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
          $null=Find-Control 'Показать экран'
          Measure-Stage "closed-$cycle"
        }
        Leave-TestCall
        Start-Sleep -Seconds 25
        Measure-Stage 'rest-final'
        return
      }
      if($env:NIMBO_QA_LARGE_CHAT){
        if($env:NIMBO_QA_CHAT_CALL_CLOSE){
          Press 'Мой профиль'
          Press ('Открыть сервер '+([uri]$env:NIMBO_NATIVE_TEST_URL).Authority)
          $null=Find-Control 'Большая история' ([Windows.Automation.ControlType]::ListItem)
        }
        $rounds=if($env:NIMBO_QA_ATTACHMENT_CYCLES){5}else{2}
        $route=$env:NIMBO_QA_RETENTION_ROUTE
        if($route){$rounds=[int]$env:NIMBO_QA_RETENTION_ROUNDS}
        foreach($round in 1..$rounds){
          if($route -in @('quiet','voice')){
            Select-TestRoom 'общий'
            Start-Sleep -Seconds 13
          }else{
            $clock=[Diagnostics.Stopwatch]::StartNew()
            $historyRoom=Find-Control 'Большая история' ([Windows.Automation.ControlType]::ListItem)
            if($route){Click-TestControl $historyRoom}
            $historyRoom.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
            $last=MediaRow 'Видео 4'
            $clock.ElapsedMilliseconds | Add-Content -LiteralPath (Join-Path $OutputDir 'history-ms.txt')
            $bounds=$root.Current.BoundingRectangle
            $point=[IntPtr](([int]($bounds.Top+$bounds.Height/2) -shl 16) -bor ([int]($bounds.Left+$bounds.Width/2) -band 0xffff))
            foreach($direction in @(1,-1)){
              $steps=if($env:NIMBO_QA_ATTACHMENT_CYCLES -and $round -gt 1){0}else{64}
              for($step=0;$step -lt $steps;$step++){
                $null=[NimboWindowQA]::PostMessage($hw,0x20A,[IntPtr](($direction*480) -shl 16),$point)
                Start-Sleep -Milliseconds 100
              }
              $edge=if($direction -gt 0){'Текст 0-0'}else{'Видео 4'}
              if($steps){
                # Older pages can arrive after the wheel sequence finishes on a busy desktop.
                for($pass=1;$pass -le 3;$pass++){
                  try {$null=Find-Control $edge ([Windows.Automation.ControlType]::ListItem);break}
                  catch {if($pass -eq 3){throw}}
                  for($step=0;$step -lt 64;$step++){
                    $null=[NimboWindowQA]::PostMessage($hw,0x20A,[IntPtr](($direction*480) -shl 16),$point)
                    Start-Sleep -Milliseconds 100
                  }
                }
              }
            }
            Measure-Stage "history-$round"
            if($env:NIMBO_QA_PLAY_CHAT_VIDEO){
              $row=MediaRow 'Видео 4'
              Click-TestControl $row 150 145
              Measure-Stage "play-$round"
              Click-TestControl $row 150 145
              Measure-Stage "pause-$round"
              if($env:NIMBO_QA_PAUSE_REST -and $round -eq 1){
                Start-Sleep -Seconds 30
                Measure-Stage 'pause-rest'
              }
              if($env:NIMBO_QA_ATTACHMENT_CYCLES){
                # Resume, then observe natural end so the next cycle starts at time zero.
                Click-TestControl $row 150 145
                Measure-Stage "play-resumed-$round"
                for($retry=0;$retry -lt 120;$retry++){
                  if([NimboWindowQA]::HitTest($hw,0x80db,[IntPtr]::Zero,[IntPtr]::Zero) -ne [IntPtr]1){throw 'Missing playback resource sample'}
                  $state=Get-Content -Raw -LiteralPath (Join-Path $data 'qa-memory.json') | ConvertFrom-Json
                  if(!$state.attachmentDecoders){break}
                  Start-Sleep -Milliseconds 250
                }
                if($retry -eq 120){throw 'Video decoder survived natural end'}
                Measure-Stage "pause-ended-$round"
              }
            }
            if($env:NIMBO_QA_ACTIVE_CAPTURE -or $env:NIMBO_QA_ATTACHMENT_CYCLES){
              $audio=Reveal-RecentRow 'Запись 4' 124
              Click-TestControl $audio 89 124
              Measure-Stage "play-audio-$round"
              Click-TestControl $audio 89 124
              Measure-Stage "pause-audio-$round"
              $photo=Reveal-RecentRow 'Галерея 4' 135
              Click-TestControl $photo 150 135
              $null=Find-Control 'Закрыть просмотр'
              Measure-Stage "photo-open-$round"
              Click-TestControl (Find-Control 'Закрыть просмотр')
              $null=Find-Control 'Гостиная' ([Windows.Automation.ControlType]::ListItem)
              Measure-Stage "photo-closed-$round"
            }
          }
          if($env:NIMBO_QA_CHAT_CALL_CLOSE -and $route -notin @('quiet','chat')){
            Select-TestRoom 'Гостиная'
            $null=Find-Control 'Выключить микрофон'
            Measure-Stage "voice-$round"
            if($env:NIMBO_QA_GAME_OVERLAY -and $round -eq 1){
              Press 'Настройки'
              Press 'Игровой оверлей'
              Measure-Stage 'overlay-before'
              function Toggle-TestOverlay {
                $control=Find-Control 'Игровой оверлей' ([Windows.Automation.ControlType]::CheckBox)
                $control.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
              }
              for($i=1;$i -le 5;$i++){
                Toggle-TestOverlay
                Measure-Stage "overlay-visible-$i"
                Toggle-TestOverlay
                Measure-Stage "overlay-hidden-$i"
              }
              Toggle-TestOverlay
              Press 'К комнате'
            }
            if($env:NIMBO_QA_ACTIVE_CAPTURE){
              Start-TestScreen
              Measure-Stage "screen-on-$round"
              Select-TestRoom 'Большая история'
              Measure-Stage "screen-chat-$round"
              Select-TestRoom 'Гостиная'
              Press 'Остановить показ экрана'
              $null=Find-Control 'Показать экран'
              Measure-Stage "screen-off-$round"
              Press 'Включить камеру'
              $null=Find-Control 'Выключить камеру'
              Measure-Stage "camera-on-$round"
              Press 'Выключить камеру'
              $null=Find-Control 'Включить камеру'
              Measure-Stage "camera-off-$round"
              if($round -eq 2){
                Start-Sleep -Seconds 30
                Measure-Stage "capture-rest-$round"
              }
            }
            Leave-TestCall
          }elseif($env:NIMBO_QA_ATTACHMENT_CYCLES -and $round -lt $rounds){
            $null=[NimboWindowQA]::PostMessage($hw,0x20A,[IntPtr]((-14400) -shl 16),$point)
            Start-Sleep -Milliseconds 300
            continue
          }else{Select-TestRoom 'общий'}
          Measure-Stage "left-$round"
        }
        Press 'Мой профиль'
        Press ('Открыть сервер '+([uri]$env:NIMBO_NATIVE_TEST_URL2).Authority)
        for($i=0;$i -lt 70;$i++){
          if((Get-Content -LiteralPath (Join-Path $data 'desktop.ini') -Raw).Contains('lastServer='+$env:NIMBO_NATIVE_TEST_URL2)){break}
          Start-Sleep -Milliseconds 100
        }
        if($i -eq 70){throw 'Server switch did not complete'}
        Select-TestRoom 'общий'
        Measure-Stage 'second-server'
        Start-Sleep -Seconds 30
        Measure-Stage 'rest-final'
        if($env:NIMBO_QA_CHAT_CALL_CLOSE){
          Press 'Мой профиль'
          Press ('Открыть сервер '+([uri]$env:NIMBO_NATIVE_TEST_URL).Authority)
          $null=Find-Control 'Большая история' ([Windows.Automation.ControlType]::ListItem)
          Select-TestRoom 'Гостиная'
          $null=Find-Control 'Выключить микрофон'
          $observerHeaders=@{Authorization='Bearer '+$env:NIMBO_QA_OBSERVER_SESSION;'X-Nimbo-Client'='1';Origin='https://desktop.nimbo.invalid'}
          function Observed-Call {
            $state=Invoke-RestMethod -Uri ($env:NIMBO_NATIVE_TEST_URL+'/api/workspace') -Headers $observerHeaders
            foreach($entry in $state.presence.PSObject.Properties){foreach($person in $entry.Value.people){$person.identity}}
          }
          $before=@()
          for($i=0;$i -lt 35 -and !$before.Count;$i++){Start-Sleep -Milliseconds 150;$before=@(Observed-Call)}
          if($before.Count -ne 1){throw 'Observer could not see exactly one caller before closing'}
          $workers=@(Get-CimInstance Win32_Process -Filter ("ParentProcessId="+$p.Id+" AND Name='NimboVoice.exe'"))
          if($workers.Count){throw 'Unexpected voice process before closing'}
          $clock=[Diagnostics.Stopwatch]::StartNew()
          if(!$p.CloseMainWindow()){throw 'Client did not accept close'}
          if(!$p.WaitForExit(6000)){throw 'Client did not exit within six seconds'}
          if($profiler){
            if(!$profiler.WaitForExit(6000) -or !(Select-String -LiteralPath $logPath -Pattern '^exit code: 0 \(' -Quiet)){throw 'Profiler did not confirm normal client exit'}
          }elseif($p.ExitCode -ne 0){throw "Client exited with code $($p.ExitCode)"}
          if(@(Observed-Call).Count){throw 'Departed caller still visible to observer after client exit'}
          $clock.ElapsedMilliseconds | Set-Content -LiteralPath (Join-Path $OutputDir 'close-and-observe-ms.txt')
        }
        return
      }
      if($env:NIMBO_QA_VOICE_CYCLES){
        function Voice-Children {
          @(Get-CimInstance Win32_Process -Filter ("ParentProcessId="+$p.Id+" AND Name='NimboVoice.exe'") | ForEach-Object ProcessId)
        }
        function Check-Voice([string]$room,[string]$phase){
          $null=Find-Control 'Выключить микрофон'
          $selected=Find-Control $room ([Windows.Automation.ControlType]::ListItem)
          if(!$selected.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected){throw "Wrong selected room at $phase"}
          $self=Find-Control 'Тестер · вы' ([Windows.Automation.ControlType]::ListItem)
          $nextRow=[Windows.Automation.TreeWalker]::ControlViewWalker.GetNextSibling($selected)
          if(!$nextRow -or $nextRow.Current.Name -ne $self.Current.Name){throw "Self missing under $room at $phase"}
          $children=@(Voice-Children)
          if($children.Count){throw "Voice ran in a child process at $phase"}
          Measure-Stage $phase
        }
        foreach($batch in 1..2){
          foreach($i in 1..4){
            Select-TestRoom 'Гостиная'
            Check-Voice 'Гостиная' "joined-$batch-$i"
            if($batch -eq 2){
              Select-TestRoom 'Проверка звука'
              Check-Voice 'Проверка звука' "switched-$i"
            }
            Leave-TestCall
            Select-TestRoom 'общий'
            Measure-Stage "left-$batch-$i"
            if(@(Voice-Children).Count){throw 'Voice worker survived leaving the room'}
          }
          if($batch -eq 1){Start-Sleep -Seconds 61}
        }
        Start-Sleep -Seconds 30
        Measure-Stage 'workspace-final'
        return
      }
      if($env:NIMBO_QA_COLD_CALLS){
        foreach($round in 1..5){
          Select-TestRoom 'Гостиная';$null=Find-Control 'Выключить микрофон'
          Measure-Stage "call-$round"
          Leave-TestCall
          Select-TestRoom 'общий'
          Start-Sleep -Seconds 25
          Measure-Stage "released-$round"
        }
        Measure-Stage 'workspace-final';return
      }
      foreach($cycle in 1..3){
        Select-TestRoom 'Замер аудио'
        # Qt's list exposes each message as a single UIA item. The audio card is
        # below its text. Click the play circle relative to that row at 100% DPI.
        $audio=MediaRow 'Замер аудио'
        Measure-Stage "audio-visible-$cycle"
        Click-TestControl $audio 89 124;Measure-Stage "playing-$cycle"
        if($cycle -eq 1){Capture "playing"}
        Click-TestControl $audio 89 124;Measure-Stage "paused-$cycle"
        if($cycle -eq 1){Capture "paused"}
        Click-TestControl $audio 89 124;Start-Sleep -Seconds 7;Measure-Stage "ended-$cycle"
        Select-TestRoom 'Замер фото';$photo=MediaRow 'Замер фото';Measure-Stage "thumbnail-$cycle"
        if($cycle -eq 1){Capture "photo-before-open"}
        Click-TestControl $photo 150 135
        $null=Find-Control 'Закрыть просмотр'
        if([NimboWindowQA]::GetLastActivePopup($hw) -ne $hw){throw 'Viewer blocked the main window'}
        $r=New-Object NimboWindowQA+Rect
        $null=[NimboWindowQA]::GetWindowRect($hw,[ref]$r)
        $pt=[IntPtr]((($r.y+16) -shl 16) -bor (($r.x+110) -band 0xffff))
        if(![NimboWindowQA]::IsWindowEnabled($hw) -or [NimboWindowQA]::HitTest($hw,0x84,[IntPtr]0,$pt).ToInt32() -ne 2){throw 'Title bar unavailable for system dragging'}
        $null=[NimboWindowQA]::MoveWindow($hw,$r.x+12,$r.y+8,$r.right-$r.x,$r.bottom-$r.y,$true)
        $null=Find-Control 'Закрыть просмотр'
        Measure-Stage "viewer-$cycle"
        Press 'Закрыть просмотр'
        $null=[NimboWindowQA]::MoveWindow($hw,$r.x,$r.y,$r.right-$r.x,$r.bottom-$r.y,$true)
        Measure-Stage "photo-closed-$cycle"
        Select-TestRoom 'общий';Measure-Stage "empty-$cycle"
      }
      Sample-Idle 'rest' @(10,30,60)
      Measure-Stage 'rest-final'
      if($env:NIMBO_QA_WORKSPACE_MEMORY){
        foreach($round in 1..3){
          Press 'Настройки'
          foreach($tab in @('Общие','Видео','Каналы','Участники и роли','Горячие клавиши','Звук')){
            Press $tab
          }
          Press 'К комнате'
          Measure-Stage "settings-closed-$round"
          Select-TestRoom 'Гостиная'
          $null=Find-Control 'Выключить микрофон'
          Measure-Stage "call-$round"
          Leave-TestCall
          Select-TestRoom 'общий';Measure-Stage "call-left-$round"
        }
        Sample-Idle 'workspace-rest' @(5,30,60)
        Measure-Stage 'workspace-final'
      }
      return
    }
    foreach($stage in @('общий','Замер вложений','общий')){
      $channel=Find-Control $stage ([Windows.Automation.ControlType]::ListItem)
      $channel.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
      $phase=if($stage -eq 'Замер вложений'){'media'}elseif(Test-Path $csv){'after'}else{'empty'}
      if($phase -eq 'media'){$null=Find-Control 'Клипы для замера памяти' ([Windows.Automation.ControlType]::ListItem); $intervals=@(5,15,30,60,90,120)
        for($t=180;$t -lt [int]$env:NIMBO_QA_IDLE_SECONDS;$t+=60){$intervals+=$t}
        if([int]$env:NIMBO_QA_IDLE_SECONDS -gt 120){$intervals+=[int]$env:NIMBO_QA_IDLE_SECONDS}
        Sample-Idle $phase $intervals
      }
      else{Sample-Idle $phase @(3,15,30)}
    }
    return
  }
  function Measure-Output([string]$label,[scriptblock]$action){
    $level=Join-Path $OutputDir ("output-"+$label+".txt")
    $tester=Join-Path $PSScriptRoot '../build-media/nimbo_desktop_tests.exe'
    $meter=Start-Process -FilePath $tester -ArgumentList @('--measure-output',$p.Id,('"'+$level+'"')) -PassThru -WindowStyle Hidden
    try{
      for($n=0;$n -lt 150 -and !(Test-Path -LiteralPath ($level+'.ready')) -and !$meter.HasExited;$n++){Start-Sleep -Milliseconds 20}
      if(!(Test-Path -LiteralPath ($level+'.ready'))){throw "Audio session meter did not start for $label"}
      & $action
      if(!$meter.WaitForExit(4000) -or $meter.ExitCode -ne 0){throw "No Windows audio output from the test EXE for $label"}
    }finally{
      if(!$meter.HasExited){$meter.Kill();$meter.WaitForExit()}
      $meter.Dispose()
    }
  }
    function Click-WindowAt($handle,$screenX,$screenY){
      $pt=New-Object NimboWindowQA+Point;$pt.x=[int]$screenX;$pt.y=[int]$screenY
      $null=[NimboWindowQA]::ScreenToClient($handle,[ref]$pt)
      $pos=[IntPtr](($pt.y -shl 16) -bor ($pt.x -band 0xffff))
      $null=[NimboWindowQA]::PostMessage($handle,0x201,[IntPtr]1,$pos)
      $null=[NimboWindowQA]::PostMessage($handle,0x202,[IntPtr]0,$pos)
    }
  if($LinkCover){
    Shot 'room'
    $channel=Find-Control 'общий' ([Windows.Automation.ControlType]::ListItem)
    $channel.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
    $labelFile=Join-Path $Fixture 'cover-label'
    for($i=0;$i -lt 120 -and !(Test-Path -LiteralPath $labelFile);$i++){Start-Sleep -Milliseconds 100}
    # Qt list accessibility exposes the message as a list item. Check the actual
    # pixels of its cover before capturing, including the publisher image.
    for($attempt=0;$attempt -lt 100;$attempt++){
      Capture 'youtube-cover'
      $frame=[Drawing.Bitmap]::FromFile((Join-Path $OutputDir 'qt-youtube-cover.png'))
      try {
        $colored=0
        for($y=180;$y -lt 360;$y+=8){for($x=350;$x -lt 750;$x+=8){
          $pixel=$frame.GetPixel($x,$y)
          if($pixel.B -gt $pixel.R+25 -and $pixel.G -gt $pixel.R+15){$colored++}
        }}
      }finally{$frame.Dispose()}
      if($colored -gt 50){break}
      Start-Sleep -Milliseconds 150
    }
    if($colored -le 50){throw 'Publisher thumbnail did not appear in the portable client'}
    Shot 'youtube-cover'
    $null=Find-Control 'Жирный текст из веба' ([Windows.Automation.ControlType]::ListItem)
    Shot 'chat-bold'
    return
  }
  if($Invitations){
    Shot 'room';Press 'Настройки';Press 'Участники и роли'
    Press 'Создать приглашение'
    $field=Find-Control 'Подпись (необязательно)' ([Windows.Automation.ControlType]::Edit)
    $field.SetFocus()
    $field.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('Для команды')
    Shot 'invite-form';Press 'Создать приглашение'
    $null=Find-Control 'Для команды';$null=Find-Control 'Только что создано';Shot 'invite-created'
    function Visible-Text([string]$name){
      $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,$name)
      return @($root.FindAll([Windows.Automation.TreeScope]::Descendants,$condition) | Where-Object {!$_.Current.IsOffscreen}).Count -gt 0
    }
    for($i=0;$i -lt 180 -and (Visible-Text 'Только что создано');$i++){Start-Sleep -Milliseconds 100}
    if(Visible-Text 'Только что создано'){throw 'New-invitation marker did not expire'}
    if(!(Visible-Text 'Для команды')){throw 'Invitation disappeared with marker'}
    [IO.File]::WriteAllText((Join-Path $Fixture 'marker-expired'),'')
    for($i=0;$i -lt 120 -and (Visible-Text 'Для команды');$i++){Start-Sleep -Milliseconds 100}
    if(Visible-Text 'Для команды'){throw 'Browser revocation did not reach desktop'}
    return
  }
  if($Settings){
    Shot 'room'
    foreach($width in @(1138,1264,1760)){
      Resize-Client $width
      foreach($expanded in @($false,$true)){
        if($expanded){Press 'Участники 1'}
        Start-Sleep -Milliseconds 300
        $name='voice-grid-'+$width+'-'+$(if($expanded){'open'}else{'closed'})
        $area=(Find-Control 'Присоединяйтесь к разговору.').Current.BoundingRectangle
        $origin=New-Object NimboWindowQA+Point;$null=[NimboWindowQA]::ClientToScreen($hw,[ref]$origin)
        @{x=($area.Left-$origin.x);y=($area.Top-$origin.y);right=($area.Right-$origin.x);height=$area.Height} | ConvertTo-Json | Set-Content (Join-Path $OutputDir ('qt-'+$name+'.json'))
        Shot $name
      }
      Press 'Участники 1';Start-Sleep -Milliseconds 300
    }
    Resize-Client 1264
    Press 'Мой профиль';$null=Find-Control 'Имя на этом сервере';Shot 'profile'
    Resize-Client 960;Shot 'profile-960';Resize-Client 1264
    Press 'Настройки'
    foreach($pair in @(@('Звук','audio'),@('Видео','video'),@('Общие','general'),@('Каналы','channels'),@('Участники и роли','roles'),@('Горячие клавиши','keys'))){
      Press $pair[0]
      $null=Find-Control ('Настройки · '+$pair[0]+$(if($pair[1] -eq 'roles'){' · 1'}else{''}))
      Shot ('settings-'+$pair[1])
      if($pair[1] -eq 'audio'){Press 'Диагностика устройств';$null=Find-Control 'Скачать журнал';Shot 'audio-diagnostics';Press 'Диагностика устройств'}
    }
    Press 'К комнате';$null=Find-Control 'Гостиная' ([Windows.Automation.ControlType]::ListItem);Shot 'room-returned';return
  }
  if($ServerReturn){
    Shot 'room'
    $room=Find-Control 'Гостиная' ([Windows.Automation.ControlType]::ListItem)
    $r=$room.Current.BoundingRectangle
    Click-WindowAt $hw ($r.Left+18) ($r.Top+$r.Height/2)
    $null=Find-Control 'Выключить микрофон'
    Press 'Настройки';Press 'Звук'
    $trigger=Find-Control 'Выбрать сервер';$r=$trigger.Current.BoundingRectangle
    Press 'Выбрать сервер'
    foreach($step in @(@('server-away',$env:NIMBO_NATIVE_TEST_URL2),@('server-back',$env:NIMBO_NATIVE_TEST_URL))){
      Start-Sleep -Milliseconds 200
      if($step[0] -eq 'server-away'){Click-WindowAt $hw ($r.Right+120) ($r.Top+92)}
      else{Press 'Вернуться в звонок · Свои люди · Гостиная'}
      $ini=Join-Path $data 'desktop.ini'
      for($n=0;$n -lt 80 -and !(Get-Content -LiteralPath $ini -Raw).Contains('lastServer='+$step[1]);$n++){Start-Sleep -Milliseconds 100}
      Press 'Выбрать сервер'
      $null=Find-Control ('Мои серверы · '+([uri]$step[1]).Authority)
      if($step[0] -eq 'server-back'){$null=Find-Control 'Выйти из голосовой комнаты'}
      Shot $step[0]
      if($step[0] -eq 'server-away'){
        Press 'Выбрать сервер'
        $null=Find-Control 'Вернуться в звонок · Свои люди · Гостиная'
        Shot 'foreign-call'
      }
    }
    return
  }
  if($AudioSelect){
    Shot 'room';Press 'Настройки';Press 'Звук'
    $combo=Find-Control 'Шумоподавление' ([Windows.Automation.ControlType]::ComboBox)
    $bounds=$combo.Current.BoundingRectangle;Click-WindowAt $hw ($bounds.Left+45) ($bounds.Top+$bounds.Height/2)
    Shot 'noise-open'
    $cond=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$p.Id)
    $popup=[Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children,$cond) | Where-Object {$_.Current.NativeWindowHandle -and $_.Current.NativeWindowHandle -ne $hw.ToInt64() -and !$_.Current.IsOffscreen} | Select-Object -First 1
    if(!$popup){throw 'No noise list'}
    $options=$popup.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ListItem))
    if($options.Count -ne 4){throw "Expected four options, got $($options.Count)"}
    $metrics=@(foreach($option in $options){$r=$option.Current.BoundingRectangle;[ordered]@{label=$option.Current.Name;height=$r.Height;top=$r.Top;left=$r.Left}})
    $metrics | ConvertTo-Json | Set-Content (Join-Path $OutputDir 'qt-noise-options.json')
    $pick=$options[1].Current.BoundingRectangle
    Click-WindowAt ([IntPtr]$popup.Current.NativeWindowHandle) ($pick.Left+30) ($pick.Top+$pick.Height/2)
    Start-Sleep -Milliseconds 200
    if($combo.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value -ne 'RNNoise'){throw 'EXE selection failed'}
    Shot 'noise-selected'
    Measure-Output 'test-tone' {
      Press 'Проверить звук'
      $labels=$root.FindAll([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.Condition]::TrueCondition) | ForEach-Object {$_.Current.Name}
      ($labels | Where-Object {$_ -match 'звук|устройство|Устройство|динамики'}) | Set-Content (Join-Path $OutputDir 'tone-controls.txt')
    }
    return
  }
  if($VoiceRejoin){
    Shot 'room'
    function Point-InControl($control,[int]$offset=0){
      $r=$control.Current.BoundingRectangle
      $pt=New-Object NimboWindowQA+Point
      $pt.x=[int]($r.Left+$(if($offset){$offset}else{$r.Width/2}));$pt.y=[int]($r.Top+$r.Height/2)
      $null=[NimboWindowQA]::ScreenToClient($hw,[ref]$pt)
      return [IntPtr](($pt.y -shl 16) -bor ($pt.x -band 0xffff))
    }
    function Click-InTestWindow($point){
      $null=[NimboWindowQA]::PostMessage($hw,0x201,[IntPtr]1,$point)
      $null=[NimboWindowQA]::PostMessage($hw,0x202,[IntPtr]0,$point)
    }
    $roomPoint=Point-InControl (Find-Control 'Гостиная' ([Windows.Automation.ControlType]::ListItem)) 18
    $startup=[Diagnostics.Stopwatch]::StartNew()
    Click-InTestWindow $roomPoint
    $exitCondition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Выйти из голосовой комнаты')
    $null=Find-Control 'Выйти из голосовой комнаты'
    $uiReady=$startup.ElapsedMilliseconds
    $exitButton=$root.FindAll([Windows.Automation.TreeScope]::Descendants,$exitCondition) | Where-Object {!$_.Current.IsOffscreen} | Sort-Object {$_.Current.BoundingRectangle.Top} -Descending | Select-Object -First 1
    $exitPoint=Point-InControl $exitButton
    $null=Find-Control 'Выключить микрофон'
    @{exitControlMs=$uiReady;mediaControlsMs=$startup.ElapsedMilliseconds} | ConvertTo-Json | Set-Content (Join-Path $OutputDir 'first-join.json')
    Measure-Output 'peer-join' { [IO.File]::WriteAllText((Join-Path $Fixture 'desktop-in-call'),'') }
    for($n=0;$n -lt 150 -and !(Test-Path -LiteralPath (Join-Path $Fixture 'web-in-call'));$n++){Start-Sleep -Milliseconds 100}
    if(!(Test-Path -LiteralPath (Join-Path $Fixture 'web-in-call'))){throw 'The web peer did not join'}
    Measure-Output 'peer-leave' { [IO.File]::WriteAllText((Join-Path $Fixture 'check-peer-leave'),'') }
    [IO.File]::WriteAllText((Join-Path $Fixture 'restore-peer'),'')
    for($n=0;$n -lt 150 -and !(Test-Path -LiteralPath (Join-Path $Fixture 'peer-restored'));$n++){Start-Sleep -Milliseconds 100}
    if(!(Test-Path -LiteralPath (Join-Path $Fixture 'peer-restored'))){throw 'Web peer did not return'}
    Measure-Output 'peer-video-on' { [IO.File]::WriteAllText((Join-Path $Fixture 'check-video-on'),'') }
    Measure-Output 'peer-video-off' { [IO.File]::WriteAllText((Join-Path $Fixture 'check-video-off'),'') }
    Measure-Output 'mute' {Press 'Выключить микрофон'}
    Measure-Output 'unmute' {Press 'Включить микрофон'}
    Shot 'voice-before-rejoin'
    foreach($width in @(946,1308)){
      Resize-Client $width;Start-Sleep -Milliseconds 200
      $null=Find-Control 'Выключить микрофон';Shot ('sidebar-call-'+$width)
    }
    Resize-Client 1264
    for($i=0;$i -lt 3;$i++){
      Click-InTestWindow $exitPoint
      Click-InTestWindow $roomPoint
      $ack=Join-Path $Fixture ("voice-cycle-"+$i)
      for($n=0;$n -lt 180 -and !(Test-Path -LiteralPath $ack);$n++){Start-Sleep -Milliseconds 100}
      if(!(Test-Path -LiteralPath $ack)){throw "Web did not confirm new connection $i"}
      $null=Find-Control 'Выйти из голосовой комнаты' 
    }
    [IO.File]::WriteAllText((Join-Path $Fixture 'native-rejoined'),'')
    Shot 'voice-after-rejoin';Click-InTestWindow $exitPoint
    [IO.File]::WriteAllText((Join-Path $Fixture 'native-left'),'')
    Start-Sleep -Milliseconds 600
    return
  }
  if($ChannelIcons){
    Shot 'room';Press 'Настройки';Press 'Каналы';Test-ChannelIcons;return
  }
  if(!$Switcher){Shot 'room';if(!$Baseline){Resize-Client 1138;Shot 'room-1138';Press 'Участники 1';Shot 'room-expanded-1138';Resize-Client 1264;Shot 'room-expanded';Press 'Участники 1'}}else{
    $channel=Find-Control 'общий' ([Windows.Automation.ControlType]::ListItem)
    $channel.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Select()
  }
  if(!$Switcher -and !$Baseline){
    function Click-QAControl($e){
      $null=[NimboWindowQA]::SetForegroundWindow($hw)
      $point=$e.GetClickablePoint()
      $pt=New-Object NimboWindowQA+Point;$pt.x=[int]$point.X;$pt.y=[int]$point.Y
      if([NimboWindowQA]::GetAncestor([NimboWindowQA]::WindowFromPoint($pt),2) -eq $hw){
        [Windows.Forms.Cursor]::Position=[Drawing.Point]::new($pt.x,$pt.y)
        [NimboWindowQA]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[NimboWindowQA]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
      }else{
        # Send the normal client mouse messages directly if another window covers the test.
        $null=[NimboWindowQA]::ScreenToClient($hw,[ref]$pt)
        $position=[IntPtr](($pt.y -shl 16) -bor ($pt.x -band 0xffff))
        foreach($message in @(0x200,0x201,0x202)){
          $held=[IntPtr]([int]($message -eq 0x201))
          if(![NimboWindowQA]::PostMessage($hw,$message,$held,$position)){throw "Mouse delivery failed for $($e.Current.Name)"}
        }
      }
    }
    function Click-SettingsControl($control){
      # Match scrollIntoView({block:"nearest"}) used by the browser scenarios.
      $condition=[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ControlTypeProperty,[Windows.Automation.ControlType]::ScrollBar)
      $bars=$root.FindAll([Windows.Automation.TreeScope]::Descendants,$condition)
      $bar=$bars | Where-Object {
        $bounds=$_.Current.BoundingRectangle
        !$bounds.IsEmpty -and $bounds.Height -gt 100 -and $bounds.Width -lt 32 -and !$_.Current.IsOffscreen
      } | Sort-Object {$_.Current.BoundingRectangle.Right} -Descending | Select-Object -First 1
      if($bar){
        $viewport=$bar.Current.BoundingRectangle
        $bounds=$control.Current.BoundingRectangle
        $offset=if($bounds.Top -lt $viewport.Top){$bounds.Top-$viewport.Top}elseif($bounds.Bottom -gt $viewport.Bottom){$bounds.Bottom-$viewport.Bottom}else{0}
        if($offset){
          $range=$bar.GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern)
          $target=[Math]::Clamp($range.Current.Value+$offset,$range.Current.Minimum,$range.Current.Maximum)
          $range.SetValue($target)
          Start-Sleep -Milliseconds 100
        }
      }
      Click-QAControl $control
    }
    Click-QAControl (Find-Control 'Гостиная' ([Windows.Automation.ControlType]::ListItem))
    $null=Find-Control 'Выйти из голосовой комнаты'
    $own=Find-Control 'Тестер · вы' ([Windows.Automation.ControlType]::ListItem)
    $r=$own.Current.BoundingRectangle
    [Windows.Forms.Cursor]::Position=[Drawing.Point]::new([int]($r.Left+28),[int]($r.Top+$r.Height/2));[NimboWindowQA]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[NimboWindowQA]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
    $null=Find-Control 'Выйти из голосовой комнаты';Shot 'voice-sidebar'
    Click-QAControl (Find-Control 'общий' ([Windows.Automation.ControlType]::ListItem))
    $null=Find-Control 'Отправить сообщение';Shot 'call-chat-closed'
    Press 'Участники 1';Shot 'call-chat-open';Press 'Участники 1'
    Press 'Выйти из голосовой комнаты'
  }
  $profile=Find-Control 'Мой профиль'
  $profile.GetCurrentPattern([Windows.Automation.InvokePattern]::Pattern).Invoke()
  if(!$Baseline){$null=Find-Control 'Имя на этом сервере'}
  if($Switcher){
    function Open-Switcher([string]$url){
      if(!$url){throw 'Missing test origin'}
      Press 'Выбрать сервер'
      $null=Find-Control ('Мои серверы · '+([uri]$url).Authority)
    }
    function Select-Other([string]$url){
      Click-Client ($menuX+110) ($menuY+100)
      for($i=0;$i -lt 80;$i++){
        $ini=Join-Path $data 'desktop.ini'
        if((Test-Path -LiteralPath $ini) -and (Get-Content -LiteralPath $ini -Raw).Contains('lastServer='+$url)){return}
        Start-Sleep -Milliseconds 100
      }
      throw 'The selected server did not become active'
    }
    function Confirm-Room {
      $room=Find-Control 'Гостиная' ([Windows.Automation.ControlType]::ListItem)
      if(!$room.GetCurrentPattern([Windows.Automation.SelectionItemPattern]::Pattern).Current.IsSelected){throw 'Wrong room after switching servers'}
      $null=Find-Control 'Присоединяйтесь к разговору.'
    }
    $null=[NimboWindowQA]::SetForegroundWindow($hw)
    $trigger=Find-Control 'Выбрать сервер';$rect=$trigger.Current.BoundingRectangle
    $origin=New-Object NimboWindowQA+Point;$null=[NimboWindowQA]::ClientToScreen($hw,[ref]$origin)
    $menuX=[int]($rect.Right-$origin.x+10);$menuY=[int]($rect.Top-$origin.y-8)
    Open-Switcher $env:NIMBO_NATIVE_TEST_URL;Shot 'servers'
    $null=[NimboWindowQA]::SetForegroundWindow($hw)
    [Windows.Forms.SendKeys]::SendWait('{TAB}{TAB}');Shot 'servers-focus'
    $null=[NimboWindowQA]::SetForegroundWindow($hw)
    if(![NimboWindowQA]::PostMessage($hw,0x100,[IntPtr]27,[IntPtr]1) -or ![NimboWindowQA]::PostMessage($hw,0x101,[IntPtr]27,[IntPtr]([long]0xC0000001L))){throw 'Unable to deliver Escape to the native menu'}
    Start-Sleep -Milliseconds 240
    $trigger=Find-Control 'Выбрать сервер'
    for($i=0;$i -lt 20 -and !$trigger.Current.HasKeyboardFocus;$i++){Start-Sleep -Milliseconds 50}
    if(!$trigger.Current.HasKeyboardFocus){throw 'Escape did not return focus to the switcher'}
    [IO.File]::WriteAllText((Join-Path $Fixture 'qt-server-escape'),'')
    Open-Switcher $env:NIMBO_NATIVE_TEST_URL
    $watch=[Diagnostics.Stopwatch]::StartNew()
    [Windows.Forms.Cursor]::Position=[Drawing.Point]::new($origin.x+$menuX+110,$origin.y+$menuY+100)
    Start-Sleep -Milliseconds 45
    [IO.File]::WriteAllText((Join-Path $OutputDir 'qt-hover-timing.json'),('{"elapsedMs":'+$watch.ElapsedMilliseconds+'}'))
    Capture 'servers-moving'
    Shot 'servers-hover' 
    Resize-Client 960;Shot 'servers-960';Resize-Client 1264;Start-Sleep -Milliseconds 300
    Select-Other $env:NIMBO_NATIVE_TEST_URL2;Confirm-Room;Open-Switcher $env:NIMBO_NATIVE_TEST_URL2;Shot 'servers-switched'
    Select-Other $env:NIMBO_NATIVE_TEST_URL;Confirm-Room;Open-Switcher $env:NIMBO_NATIVE_TEST_URL;Shot 'servers-returned'
    return
  }
  Shot 'profile'
  if(!$Baseline){
    $avatar=Find-Control 'Загрузить аватар';$point=$avatar.GetClickablePoint();[Windows.Forms.Cursor]::Position=[Drawing.Point]::new([int]$point.X,[int]$point.Y);Shot 'profile-hover'
    Click-QAControl (Find-Control 'Скачать профиль');Shot 'export';Click-QAControl (Find-Control 'Отмена');Click-QAControl (Find-Control 'Добавить сервер');Shot 'server-add';Click-QAControl (Find-Control 'Отмена')
    $edit=Find-Control 'Имя на этом сервере' ([Windows.Automation.ControlType]::Edit)
    if($edit.Current.ControlType -ne [Windows.Automation.ControlType]::Edit){throw 'The profile field must be editable'}
    $edit.SetFocus();Shot 'profile-focus'
    $edit.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('he he');Press 'Сохранить профиль';$null=Find-Control 'Профиль сохранён.';Shot 'footer-name'
    $edit=Find-Control 'Имя на этом сервере' ([Windows.Automation.ControlType]::Edit);$edit.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('Тестер');Press 'Сохранить профиль'
    Resize-Client 960;Shot 'profile-960';Resize-Client 1264
    Press 'Настройки'
    foreach($pair in @(@('Звук','audio'),@('Видео','video'),@('Общие','general'),@('Каналы','channels'),@('Участники и роли','roles'),@('Горячие клавиши','keys'))){
      $tab=Find-Control $pair[0] ([Windows.Automation.ControlType]::Button)
      Click-QAControl $tab
      $null=Find-Control ('Настройки · '+$pair[0]+$(if($pair[1] -eq 'roles'){' · 1'}else{''}))
      if($pair[1] -eq 'general'){$null=Find-Control 'Занятое место'}
      Shot ('settings-'+$pair[1])
      if($pair[1] -eq 'audio'){
        Press 'Диагностика устройств';$null=Find-Control 'Скачать журнал';Shot 'audio-diagnostics';Press 'Диагностика устройств'
      }
      if($pair[1] -eq 'channels'){
        $at=New-Object NimboWindowQA+Point;$at.x=850;$at.y=520;$null=[NimboWindowQA]::ClientToScreen($hw,[ref]$at)
        [Windows.Forms.Cursor]::Position=[Drawing.Point]::new($at.x,$at.y)
        for($j=0;$j -lt 12;$j++){[NimboWindowQA]::mouse_event(0x800,0,0,120,[UIntPtr]::Zero)}
        $field=Find-Control 'Название сервера' ([Windows.Automation.ControlType]::Edit)
        $field.SetFocus();Shot 'settings-channels-top'
        Press 'Изменить секцию Голосовые'
        $field=Find-Control 'Название секции' ([Windows.Automation.ControlType]::Edit)
        $field.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('Проверка секции');Shot 'section-edit'
        Press 'Отменить переименование';$null=Find-Control 'Изменить секцию Голосовые'
        Test-ChannelIcons
        Press 'Добавить канал: Текстовые';$field=Find-Control 'Название нового канала'
        $field.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('Временный канал');Shot 'channel-new'
        Press 'Отменить изменения канала';$null=Find-Control 'Изменить общий'
        $check=Find-Control 'Разрешить файлы до 20 МБ';$toggle=$check.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern)
        if($toggle.Current.ToggleState -ne [Windows.Automation.ToggleState]::Off){throw 'Unexpected fixture file policy'}
        Click-SettingsControl $check;Click-SettingsControl (Find-Control 'Сохранить' ([Windows.Automation.ControlType]::Button))
        $null=Find-Control 'Антивирус не настроен. Администратор хоста должен подключить ClamAV.'
        Shot 'files-rejected';Click-SettingsControl $check;Click-SettingsControl (Find-Control 'Сохранить' ([Windows.Automation.ControlType]::Button));Press 'Видео';Press 'Каналы'
        $check=Find-Control 'Разрешить файлы до 20 МБ'
        for($i=0;$i -lt 50 -and !$check.Current.IsEnabled;$i++){Start-Sleep -Milliseconds 100}
        if($check.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Current.ToggleState -ne [Windows.Automation.ToggleState]::Off){throw 'Invalid policy persisted'}

      }
      if($pair[1] -eq 'video'){
        $null=[NimboWindowQA]::SetForegroundWindow($hw)
        $blur=Find-Control 'Размытый фон';$blur.SetFocus()
        [Windows.Forms.SendKeys]::SendWait(' ')
        $slider=Find-Control 'Сила размытия' ([Windows.Automation.ControlType]::Slider)
        $range=$slider.GetCurrentPattern([Windows.Automation.RangeValuePattern]::Pattern);$range.SetValue(28)
        if($range.Current.Value -ne 28){throw 'Blur strength did not persist'}
        Shot 'video-blur';Resize-Client 960;Shot 'video-blur-960';Resize-Client 1264
        $off=Find-Control 'Без эффектов';$pt=$off.GetClickablePoint()
        [Windows.Forms.Cursor]::Position=[Drawing.Point]::new([int]$pt.X,[int]$pt.Y)
        [NimboWindowQA]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[NimboWindowQA]::mouse_event(4,0,0,0,[UIntPtr]::Zero)
        Start-Sleep -Milliseconds 150
        if((Get-Content -LiteralPath (Join-Path $data 'desktop.ini') -Raw) -notmatch '(?m)^blur=false'){throw 'Blur stayed enabled'}
      }
      if($pair[1] -eq 'keys'){
        function Assert-Binding([string]$code,[bool]$chord=$false){
          for($i=0;$i -lt 40;$i++){
            $cipher=[IO.File]::ReadAllBytes((Join-Path $data 'profile.dpapi'))
            $plain=[Security.Cryptography.ProtectedData]::Unprotect($cipher,$null,[Security.Cryptography.DataProtectionScope]::CurrentUser)
            $bound=([Text.Encoding]::UTF8.GetString($plain)|ConvertFrom-Json).hotkeys.mic
            if(!$code -and !$bound){return}
            if($bound.code -eq $code -and $bound.ctrl -eq $chord -and $bound.alt -eq $chord -and !$bound.shift -and !$bound.meta){return}
            Start-Sleep -Milliseconds 100
          }
          throw "Expected persisted microphone binding [$code], found [$($bound.code)]"
        }
        $null=[NimboWindowQA]::SetForegroundWindow($hw)
        Press 'Сочетание: Микрофон';$null=Find-Control 'Отмена';Shot 'keys-record'
        # Deliver F8 to this test window even when a separate desktop application holds foreground focus.
        $bindingControl=Find-Control 'Сочетание: Микрофон'
        $bindingControl.SetFocus()
        if(![NimboWindowQA]::PostMessage($hw,0x100,[IntPtr]119,[IntPtr]0x420001) -or
           ![NimboWindowQA]::PostMessage($hw,0x101,[IntPtr]119,[IntPtr]([long]0xC0420001L))){throw 'Unable to deliver F8 to test window'}
        Start-Sleep -Milliseconds 200;Assert-Binding 'F8';Shot 'keys-assigned'
        Press 'Убрать сочетание: Микрофон';Assert-Binding ''
        Press 'Сочетание: Микрофон'
        $null=Find-Control 'Отмена'
        $null=[NimboWindowQA]::SetForegroundWindow($hw)
        $b=Find-Control 'Сочетание: Микрофон';$bounds=$b.Current.BoundingRectangle
        $pt=[Drawing.Point]::new([int]($bounds.X+$bounds.Width/2),[int]($bounds.Y+$bounds.Height/2))
        [Windows.Forms.Cursor]::Position=[Drawing.Point]::new([int]$pt.X,[int]$pt.Y)
        [NimboWindowQA]::mouse_event(128,0,0,1,[UIntPtr]::Zero);[NimboWindowQA]::mouse_event(256,0,0,1,[UIntPtr]::Zero)
        Start-Sleep -Milliseconds 200;Assert-Binding 'Mouse4';Shot 'keys-mouse'
        Press 'Вернуть стандартные';Assert-Binding 'KeyM' $true
      }
      if($pair[1] -eq 'roles'){
        Press 'Создать роль'
        $field=Find-Control 'Название роли' ([Windows.Automation.ControlType]::Edit)
        $field.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('Проверка роли')
        $check=Find-Control 'Название, подпись и аватар сервера' ([Windows.Automation.ControlType]::CheckBox)
        $check.GetCurrentPattern([Windows.Automation.TogglePattern]::Pattern).Toggle()
        Shot 'role-create';Press 'Отмена';$null=Find-Control 'Создать роль'
        Press 'Изменить роль Владелец';$null=Find-Control 'Название роли' ([Windows.Automation.ControlType]::Edit)
        Shot 'role-edit';Press 'Сохранить';$null=Find-Control 'Изменить роль Владелец'
        $create=Find-Control 'Создать приглашение' ([Windows.Automation.ControlType]::Button)
        Click-SettingsControl $create;$null=Find-Control 'Тип приглашения';Shot 'invitation-form'
        Press 'Отмена'
        $search=Find-Control 'Поиск участников' ([Windows.Automation.ControlType]::Edit)
        Click-SettingsControl $search;$pattern=$search.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)
        $pattern.SetValue('нет такого имени');$null=Find-Control 'Никого не нашли. Попробуйте другое имя.';Shot 'member-search-empty'
        $pattern.SetValue('')
      }
      if($pair[1] -eq 'general'){
        $language=Find-Control 'Язык' ([Windows.Automation.ControlType]::ComboBox)
        Click-QAControl $language
        Shot 'language-open'
        [Windows.Forms.SendKeys]::SendWait('{ESC}')
        $null=[NimboWindowQA]::SetForegroundWindow($hw)
        $p0=New-Object NimboWindowQA+Point;$p0.x=850;$p0.y=620;$null=[NimboWindowQA]::ClientToScreen($hw,[ref]$p0)
        [Windows.Forms.Cursor]::Position=[Drawing.Point]::new($p0.x,$p0.y)
        for($j=0;$j -lt 12;$j++){[NimboWindowQA]::mouse_event(0x800,0,0,4294967176,[UIntPtr]::Zero)}
        Start-Sleep -Milliseconds 260;Shot 'antivirus-card'
        $field=Find-Control 'Лимит хранения, ГБ' ([Windows.Automation.ControlType]::Edit)
        $value=$field.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern)
        for($i=0;$i -lt 40 -and !$field.Current.IsEnabled;$i++){Start-Sleep -Milliseconds 100}
        $original=$value.Current.Value
        if(!$original){throw 'Storage limit did not load'}
        Click-SettingsControl $field;$value.SetValue('0.125');Shot 'storage-edit'
        Press 'Видео';Press 'Общие';$null=Find-Control 'Занятое место'
        $field=Find-Control 'Лимит хранения, ГБ' ([Windows.Automation.ControlType]::Edit)
        for($i=0;$i -lt 40 -and !$field.Current.IsEnabled;$i++){Start-Sleep -Milliseconds 100}
        $restored=$field.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).Current.Value
        if($restored -ne $original){throw "Storage limit after cancelling: [$restored], expected [$original]"}
        Shot 'storage-restored'
        Click-SettingsControl (Find-Control 'Сохранить настройки');$null=Find-Control 'Настройки сохранены.';Shot 'storage-saved'
      }
    }
    Press 'К комнате'
    $room=Find-Control 'общий'
    $null=[NimboWindowQA]::SetForegroundWindow($hw);$point=$room.GetClickablePoint()
    [Windows.Forms.Cursor]::Position=[Drawing.Point]::new([int]$point.X,[int]$point.Y);[NimboWindowQA]::mouse_event(2,0,0,0,[UIntPtr]::Zero);[NimboWindowQA]::mouse_event(4,0,0,0,[UIntPtr]::Zero)

    $null=Find-Control 'Проверка переписки';Shot 'chat'
    $null=Find-Control 'Проверенный ответ'
    Press 'Участники 1';Shot 'chat-reply'
    Press 'Участники 1'
    function Message-Actions {
      $row=Find-Control 'Проверенный ответ' ([Windows.Automation.ControlType]::ListItem)
      $r=$row.Current.BoundingRectangle
      $origin=New-Object NimboWindowQA+Point;$null=[NimboWindowQA]::ClientToScreen($hw,[ref]$origin)
      $x=[int]($r.Right-$origin.x-30);$y=[int]($r.Top-$origin.y+18)
      [Windows.Forms.Cursor]::Position=[Drawing.Point]::new([int]$r.Left+18,[int]$r.Top+16)
      Start-Sleep -Milliseconds 170
      Capture 'message-toolbar-probe'
      $bmp=[Drawing.Bitmap]::new((Join-Path $OutputDir 'qt-message-toolbar-probe.png'))
      try{
        for($dy=-14;$dy -le 18;$dy++){
          $seen=0
          for($cx=$bmp.Width-1;$cx -ge $x-135;$cx--){
            if($cx -lt 0 -or $cx -ge $bmp.Width -or $y+$dy -lt 0 -or $y+$dy -ge $bmp.Height){continue}
            $c=$bmp.GetPixel($cx,$y+$dy)
            if($c.R -eq 38 -and $c.G -eq 38 -and $c.B -eq 38){$seen++;if($seen -eq 1){$right=$cx}}
            else{$seen=0}
            if($seen -gt 110){return @(([int]$right-15),[int]($r.Top-$origin.y+22))}
          }
        }
        throw "Message hover toolbar missing at $x,$y"
      }finally{$bmp.Dispose()}
    }
    $bar=Message-Actions;Shot 'message-actions'
    Click-Client ($bar[0]-32) $bar[1];Shot 'message-edit'
    $null=[NimboWindowQA]::SetForegroundWindow($hw)
    [Windows.Forms.SendKeys]::SendWait('{ESC}');Start-Sleep -Milliseconds 250
    $bar=Message-Actions;Click-Client ($bar[0]-64) $bar[1];$null=Find-Control 'Отменить ответ';Shot 'message-answer';Click-QAControl (Find-Control 'Отменить ответ')
    $bar=Message-Actions;Click-Client $bar[0] $bar[1];$null=Find-Control 'Удалить это сообщение?';Shot 'message-remove';Click-QAControl (Find-Control 'Отмена' ([Windows.Automation.ControlType]::Button))
    # Qt's Windows list item provider exposes message rows, but not their child buttons.
    # Locate the actual rendered card and exercise its controls with client coordinates.
    for($i=0;$i -lt 100 -and !(Test-Path (Join-Path $Fixture 'web-audio-attachment'));$i++){Start-Sleep -Milliseconds 100}
    $css=Get-Content (Join-Path $OutputDir 'web-colors.json') -Raw -Encoding utf8 | ConvertFrom-Json
    $rect=$css.'audio-attachment'.'.audio-card'.rect
    $cardX=[int]$rect[0];$cardW=[int]$rect[2];$cardY=-1
    $probe=Join-Path $OutputDir 'qt-audio-probe.png'
    for($i=0;$i -lt 60 -and $cardY -lt 0;$i++){
      Capture 'audio-probe';$im=[Drawing.Bitmap]::new($probe)
      try {
        for($y=75;$y -lt $im.Height-100;$y++){
          $c=$im.GetPixel($cardX+135,$y)
          if($c.R -eq 27 -and $c.G -eq 29 -and $c.B -eq 29){$cardY=$y;break}
        }
      } finally {$im.Dispose()}
      if($cardY -lt 0){Start-Sleep -Milliseconds 150}
    }
    if($cardY -lt 0){throw 'Native inline audio card missing'}
    function Get-PlayedFraction {
      Capture 'audio-probe';$im=[Drawing.Bitmap]::new($probe)
      $last=-1;$timelineStart=$cardX+72;$timelineWidth=$cardW-88
      try{
        for($x=0;$x -lt $timelineWidth;$x++){
          for($y=$cardY+65;$y -le $cardY+77;$y++){
            $pcolor=$im.GetPixel($timelineStart+$x,$y)
            if($pcolor.R -ge 170 -and $pcolor.B -gt $pcolor.R -and $pcolor.G -gt $pcolor.R){$last=$x;break}
          }
        }
      }finally{$im.Dispose()}
      return [Math]::Max(0.0,[double]$last/$timelineWidth)
    }
    $playPoint=New-Object NimboWindowQA+Point;$playPoint.x=$cardX+37;$playPoint.y=$cardY+55
    $null=[NimboWindowQA]::ClientToScreen($hw,[ref]$playPoint)
    [Windows.Forms.Cursor]::Position=[Drawing.Point]::new($playPoint.x,$playPoint.y)
    Shot 'audio-attachment'
    $null=[NimboWindowQA]::SetForegroundWindow($hw)
    Click-Client ($cardX+37) ($cardY+55)
    for($i=0;$i -lt 20 -and (Get-PlayedFraction) -lt .015;$i++){Start-Sleep -Milliseconds 100}
    if((Get-PlayedFraction) -lt .015){throw 'Native MP3 playback did not advance'}
    Click-Client ($cardX+37) ($cardY+55)
    Click-Client ($cardX+72+[int](($cardW-88)/2)) ($cardY+64)
    Start-Sleep -Milliseconds 150;$progress=Get-PlayedFraction
    if($progress -lt .45 -or $progress -gt .55){throw "Native MP3 seek failed: $progress"}
    Start-Sleep -Milliseconds 400
    if([Math]::Abs((Get-PlayedFraction)-$progress) -gt .02){throw 'Playback continued while paused'}
    [IO.File]::WriteAllText((Join-Path $OutputDir 'qt-audio-state.json'),(@{paused=$true;progress=$progress;card=@($cardX,$cardY,$cardW,110)}|ConvertTo-Json))
    Shot 'audio-seek'
    Press 'Добавить эмодзи' 
    $popup=[Windows.Automation.AutomationElement]::RootElement.FindAll([Windows.Automation.TreeScope]::Children,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::ProcessIdProperty,$p.Id)) | Where-Object { $_.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Найти эмодзи')) } | Select-Object -First 1
    if(!$popup){throw 'Emoji search did not open'}
    $search=$popup.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'Найти эмодзи'))
    $search.GetCurrentPattern([Windows.Automation.ValuePattern]::Pattern).SetValue('кофе')
    $coffee=$popup.FindFirst([Windows.Automation.TreeScope]::Descendants,[Windows.Automation.PropertyCondition]::new([Windows.Automation.AutomationElement]::NameProperty,'кофе'))
    $r=$coffee.Current.BoundingRectangle
    $point=New-Object NimboWindowQA+Point
    $point.x=[int]($r.Left+$r.Width/2);$point.y=[int]($r.Top+$r.Height/2)
    $null=[NimboWindowQA]::ScreenToClient($hw,[ref]$point)
    $position=[IntPtr](($point.y -shl 16) -bor ($point.x -band 0xffff))
    $null=[NimboWindowQA]::PostMessage($hw,0x201,[IntPtr]1,$position)
    $null=[NimboWindowQA]::PostMessage($hw,0x202,[IntPtr]0,$position)
    for($i=0;$i -lt 40 -and !$coffee.Current.BoundingRectangle.IsEmpty -and !$coffee.Current.IsOffscreen;$i++){Start-Sleep -Milliseconds 50}
;$send=Find-Control 'Отправить сообщение';for($i=0;$i -lt 40 -and !$send.Current.IsEnabled;$i++){Start-Sleep -Milliseconds 100};if(!$send.Current.IsEnabled){throw 'Send did not enable after emoji'};Press 'Отправить сообщение';$null=Find-Control '☕'
    $null=Find-Control 'Проверка видео'
    # The fixture is a green flower video. Wait for its decoded poster inside the chat pane.
    $posterReady=$false
    for($attempt=0;$attempt -lt 40 -and !$posterReady;$attempt++){
      Capture 'video-probe'
      $probe=[Drawing.Bitmap]::new((Join-Path $OutputDir 'qt-video-probe.png'))
      try{
        $leafSamples=0;$petalSamples=0
        for($y=160;$y -lt 716;$y+=12){for($x=340;$x -lt 825;$x+=12){
          $color=$probe.GetPixel($x,$y)
          if($color.G -gt 55 -and $color.G -gt $color.B*1.5){$leafSamples++}
          if($color.R -gt 95 -and $color.R -gt $color.G*1.5 -and $color.R -gt $color.B*1.5){$petalSamples++}
        }}
        $posterReady=$leafSamples -gt 100 -and $petalSamples -gt 15
      }finally{$probe.Dispose()}
      if(!$posterReady){Start-Sleep -Milliseconds 150}
    }
    if(!$posterReady){throw 'Native inline video did not display the decoded flower poster'}
    Shot 'chat-video' 
  }
}finally{
  try{
    if($heapEnabled -and !$p.HasExited){& wpr.exe -snapshotconfig heap -pid $p.Id disable}
    if($traceStarted){& wpr.exe -cancel -instancename $traceSession}
  }finally{
    if(!$p.HasExited){$p.Kill();$p.WaitForExit()}
    $p.Dispose()
    if($profiler){if(!$profiler.WaitForExit(5000)){$profiler.Kill()};$profiler.Dispose()}
  }
}
