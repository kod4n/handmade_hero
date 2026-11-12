-- Project-local neovim config. Neovim loads it when 'exrc' is on (:help exrc),
-- and asks you to trust it the first time and again whenever it changes.

-- Builds run build.bat through cmd.exe directly instead of :make. :make goes
-- through 'shell', and with Git Bash as the shell it fails (E40): bash mangles
-- the backslashes in the Windows temp-file path it writes errors to. Running
-- it ourselves also makes the build async, so the editor doesn't freeze.
--
-- build.bat prints clang-style file:line:col: diagnostics, which the default
-- 'errorformat' already parses, so quickfix entries jump to the right place.

-- The project root is wherever this file lives. Not getcwd(): neovim also
-- finds this file when started from a subdirectory such as src/.
-- Backslashes because cmd.exe can read a /-separated path as a switch.
local root = vim.fn.fnamemodify(debug.getinfo(1, "S").source:sub(2), ":p:h"):gsub("/", "\\")

local building = false

local function build(on_success)
	if building then
		vim.notify("build already running", vim.log.levels.WARN)
		return
	end
	building = true
	vim.notify("building...")

	-- Compiler errors arrive on stderr and build.bat's echo lines on stdout;
	-- collecting both into one list as they stream in keeps them in order.
	local output = {}
	local function collect(_, data)
		if data then
			table.insert(output, data)
		end
	end

	-- Absolute path: cmd doesn't always search the working directory (e.g.
	-- when NoDefaultCurrentDirectoryInExePath is set).
	local cmd = { "cmd.exe", "/c", root .. "\\build.bat" }
	vim.system(cmd, { cwd = root, text = true, stdout = collect, stderr = collect }, function(result)
		vim.schedule(function()
			building = false
			local lines = vim.split(table.concat(output), "\r?\n", { trimempty = true })
			vim.fn.setqflist({}, " ", { title = "build.bat", lines = lines })

			if result.code == 0 then
				vim.cmd("cclose")
				vim.notify("build succeeded")
				if on_success then
					on_success()
				end
			else
				-- copen rather than cwindow: link errors don't have a file:line, so
				-- they aren't "valid" entries, but you still want to see them.
				vim.cmd("copen")
				vim.notify("build failed", vim.log.levels.ERROR)
			end
		end)
	end)
end

-- The exe finds game.dll relative to its own path, so the working directory
-- doesn't matter. It's launched through `start` because vim.system spawns
-- processes with SW_HIDE, and the game window honors that and stays invisible.
-- `start` launches it with normal show state and returns right away, so the
-- game runs independently of neovim. The "" is start's window title argument.
local function run()
	local build_dir = root .. "\\build"
	vim.system({ "cmd.exe", "/c", "start", "", build_dir .. "\\win32.exe" }, { cwd = build_dir })
end

vim.api.nvim_create_user_command("Build", function()
	build()
end, { desc = "Build with build.bat" })
vim.api.nvim_create_user_command("BuildRun", function()
	build(run)
end, { desc = "Build with build.bat, then run the game" })

vim.keymap.set("n", "<leader>mb", "<cmd>Build<CR>", { desc = "[M]ake: [B]uild" })
vim.keymap.set("n", "<leader>mx", "<cmd>BuildRun<CR>", { desc = "[M]ake: build and e[X]ecute" })

-- <F5> (from your dap config) builds with build.bat and launches
-- "Debug Game" from .vscode/launch.json, so debugging needs nothing here.
