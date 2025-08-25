#
# BlackhatOS Windows Launcher (blackhat.ps1)
# Interfaces with the BlackhatOS NT kernel driver.
#

# --- Configuration ---
$installDir = $PSScriptRoot
$configFile = Join-Path $installDir "..\config\blackhat.conf" # Correct path to config
$controlPipe = \\.\pipe\blackhatctl
$consolePipe = \\.\pipe\blackhat-console

# Assume mock driver is in a 'build_win/Debug' directory relative to the project root
$projectRoot = Split-Path $installDir -Parent
$mockDriverPath = Join-Path $projectRoot "build_win\Debug\blackhatos-win-driver-stub.exe" # Adjust Debug/Release as needed

# --- 1. Read and Parse Configuration ---
Write-Host "Booting BlackhatOS (stub)... 🎩⚡"
if (-not (Test-Path $configFile)) {
    Write-Error "Error: blackhat.conf not found at $configFile"
    Exit 1
}
$configLines = Get-Content $configFile | Where-Object { $_ -and $_ -notmatch '^\s*#' }

# --- Start Mock Driver (if not running) ---
if (-not (Get-Process -Name "blackhatos-win-driver-stub" -ErrorAction SilentlyContinue)) {
    Write-Host "Starting mock driver stub..."
    Start-Process -FilePath $mockDriverPath -WindowStyle Hidden -PassThru | Out-Null
    Start-Sleep -Seconds 2 # Give it a moment to create pipes
} else {
    Write-Host "Mock driver stub already running."
}

# --- 2. Send Configuration to Driver (Stub) ---
Write-Host "Sending config to mock driver..."
try {
    $pipe = New-Object System.IO.Pipes.NamedPipeClientStream(".", "blackhatctl", [System.IO.Pipes.PipeDirection]::Out)
    $pipe.Connect(5000) # 5 second timeout
    $writer = New-Object System.IO.StreamWriter($pipe)
    
    foreach ($line in $configLines) {
        $writer.WriteLine($line)
    }
    $writer.Flush()
    $pipe.Close()
    Write-Host "Config sent successfully."
} catch {
    Write-Error "Failed to connect to mock driver control pipe. Is it running?"
    Exit 1
}

Write-Host "BlackhatOS ready."

# --- 3. Attach to Console (Conceptual) ---
Write-Host "Attaching to console pipe... (Type 'exit' or 'quit' to end session)"
try {
    $consolePipeName = "blackhat-console"
    $pipeClient = New-Object System.IO.Pipes.NamedPipeClientStream(".", $consolePipeName, [System.IO.Pipes.PipeDirection]::InOut)
    $pipeClient.Connect(5000)

    $reader = New-Object System.IO.StreamReader($pipeClient)
    $writer = New-Object System.IO.StreamWriter($pipeClient)
    $writer.AutoFlush = $true # Ensure immediate writes

    # Start a background job to continuously read from the pipe and write to console
    $scriptBlock = {
        param($reader)
        while (-not $reader.EndOfStream) {
            $line = $reader.ReadLine()
            Write-Host $line
        }
    }
    $readerJob = Start-Job -ScriptBlock $scriptBlock -ArgumentList $reader

    # Main loop: Read from console input and write to pipe
    while ($true) {
        $inputLine = Read-Host -Prompt "" # Read user input without a prompt
        $writer.WriteLine($inputLine)

        if ($inputLine -eq "exit" -or $inputLine -eq "quit") {
            break # Exit loop if user types 'exit' or 'quit'
        }
    }

    # Clean up background job
    Stop-Job $readerJob
    Receive-Job $readerJob # Get any remaining output
    Remove-Job $readerJob

    $pipeClient.Close()
} catch {
    Write-Error "Failed to connect to mock driver console pipe or error during session."
    Write-Error $_.Exception.Message
}

Write-Host "BlackhatOS stub session ended."
