-- Project-local neovim config. Neovim loads it when 'exrc' is on (:help exrc),
-- and asks you to trust it the first time and again whenever it changes.
--
-- The same bindings work on Windows, macOS and Linux; each OS runs its own
-- scripts:
--
--            build          test       game
--   Windows  build.bat      test.bat   build\win32.exe
--   macOS    mac_build      test.sh    build/hh_macos
--   Linux    linux_build    test.sh    build/hh_linux

-- Scripts run through vim.system instead of :make. On Windows :make goes
-- through 'shell', and with Git Bash as the shell it fails (E40): bash mangles
-- the backslashes in the Windows temp-file path it writes errors to. Running
-- them ourselves also makes them async, so the editor doesn't freeze.
--
-- clang++ prints file:line:col: diagnostics, which the default 'errorformat'
-- parses. clang-cl prints MSVC-style file(line,col): error: message, which it
-- doesn't, so on Windows clang_cl_efm adds that format for the quickfix list
-- only, without changing 'errorformat' globally.

local is_windows = vim.fn.has("win32") == 1
local is_mac = vim.fn.has("mac") == 1
local sep = is_windows and "\\" or "/"

-- The project root is wherever this file lives. Not getcwd(): neovim also
-- finds this file when started from a subdirectory such as src/.
-- On Windows, backslashes because cmd.exe can read a /-separated path as a switch.
local root = vim.fn.fnamemodify(debug.getinfo(1, "S").source:sub(2), ":p:h")
if is_windows then
	root = root:gsub("/", "\\")
end

local scripts
if is_windows then
	scripts = { build = "build.bat", test = "test.bat", game = "win32.exe" }
elseif is_mac then
	scripts = { build = "mac_build", test = "test.sh", game = "hh_macos" }
else
	scripts = { build = "linux_build", test = "test.sh", game = "hh_linux" }
end

local efm = vim.o.errorformat
if is_windows then
	local clang_cl_efm = table.concat({
		[[%f(%l\,%c): %trror: %m]],
		[[%f(%l\,%c): %tarning: %m]],
		[[%f(%l\,%c): %m]],
	}, ",")
	efm = clang_cl_efm .. "," .. efm
end

-- Only one script at a time: the build and test scripts all write to build/.
local running = nil

-- Runs a script from the project root without blocking the editor and puts its
-- output in the quickfix list. on_done(ok, lines) gets the exit status and the
-- output lines. The quickfix list closes on success and opens on failure.
local function run_script(script, on_done)
	if running then
		vim.notify(running .. " already running", vim.log.levels.WARN)
		return
	end
	running = script
	vim.notify(script .. "...")

	-- Compiler errors arrive on stderr and the script's echo lines on stdout;
	-- collecting both into one list as they stream in keeps them in order.
	local output = {}
	local function collect(_, data)
		if data then
			table.insert(output, data)
		end
	end

	-- Absolute path: cmd doesn't always search the working directory (e.g.
	-- when NoDefaultCurrentDirectoryInExePath is set). On macOS and Linux the
	-- scripts go through sh, so they don't depend on the executable bit.
	local path = root .. sep .. script
	local cmd = is_windows and { "cmd.exe", "/c", path } or { "sh", path }
	vim.system(cmd, { cwd = root, text = true, stdout = collect, stderr = collect }, function(result)
		vim.schedule(function()
			running = nil
			local lines = vim.split(table.concat(output), "\r?\n", { trimempty = true })
			vim.fn.setqflist({}, " ", { title = script, lines = lines, efm = efm })

			local ok = result.code == 0
			if ok then
				vim.cmd("cclose")
			else
				-- copen rather than cwindow: link errors and test crashes don't have a
				-- file:line, so they aren't "valid" entries, but you still want to see them.
				vim.cmd("copen")
			end
			on_done(ok, lines)
		end)
	end)
end

local function build(on_success)
	run_script(scripts.build, function(ok)
		if ok then
			vim.notify("build succeeded")
			if on_success then
				on_success()
			end
		else
			vim.notify("build failed", vim.log.levels.ERROR)
		end
	end)
end

-- The test script builds and runs src/tests.cpp. A failed CHECK prints
-- "file:line: expression", which 'errorformat' turns into a quickfix entry, so
-- :cnext steps through failures the same way it does through compile errors.
local function test()
	run_script(scripts.test, function(ok, lines)
		-- The last line is the summary: "all N checks passed" or "N of M checks FAILED".
		local summary = lines[#lines] or ""
		if ok then
			vim.notify("tests: " .. summary)
		else
			vim.notify("tests failed: " .. summary, vim.log.levels.ERROR)
		end
	end)
end

-- The game finds its hot-reloadable library relative to its own path, so the
-- working directory doesn't matter.
--
-- On Windows it's launched through `start` because vim.system spawns processes
-- with SW_HIDE, and the game window honors that and stays invisible. `start`
-- launches it with normal show state and returns right away. The "" is start's
-- window title argument.
--
-- Elsewhere it's started detached, so it keeps running if neovim exits. Its
-- output is discarded rather than collected: the game logs for as long as it
-- runs, and nothing would ever read it.
local function run()
	local build_dir = root .. sep .. "build"
	local game = build_dir .. sep .. scripts.game
	if is_windows then
		vim.system({ "cmd.exe", "/c", "start", "", game }, { cwd = build_dir })
	else
		vim.system({ game }, { cwd = build_dir, detach = true, stdout = false, stderr = false })
	end
end

vim.api.nvim_create_user_command("Build", function()
	build()
end, { desc = "Build with " .. scripts.build })
vim.api.nvim_create_user_command("BuildRun", function()
	build(run)
end, { desc = "Build with " .. scripts.build .. ", then run the game" })
vim.api.nvim_create_user_command("Test", test, { desc = "Build and run the unit tests with " .. scripts.test })

vim.keymap.set("n", "<leader>mb", "<cmd>Build<CR>", { desc = "[M]ake: [B]uild" })
vim.keymap.set("n", "<leader>mx", "<cmd>BuildRun<CR>", { desc = "[M]ake: build and e[X]ecute" })
vim.keymap.set("n", "<leader>mt", "<cmd>Test<CR>", { desc = "[M]ake: run [T]ests" })

-- <F5> (from your dap config) builds with build.bat and launches
-- "Debug Game" from .vscode/launch.json, so debugging needs nothing here.
-- That launch config is Windows-only.
