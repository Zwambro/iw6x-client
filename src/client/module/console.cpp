#include <std_include.hpp>
#include "loader/module_loader.hpp"
#include "game/game.hpp"
#include "scheduler.hpp"

class console final : public module
{
public:
	console()
	{
		ShowWindow(GetConsoleWindow(), SW_HIDE);
		
		_pipe(this->handles_, 1024, _O_TEXT);
		_dup2(this->handles_[1], 1);
		_dup2(this->handles_[1], 2);

		//setvbuf(stdout, nullptr, _IONBF, 0);
		//setvbuf(stderr, nullptr, _IONBF, 0);
	}

	void post_start() override
	{
		scheduler::frame([this]()
		{
			this->log_messages();
			return scheduler::cond_continue;
		});

		this->console_runner_ = std::thread([this]
		{
			this->runner();
		});
	}

	void pre_destroy() override
	{
		this->terminate_runner_ = true;

		printf("\r\n");
		_flushall();

		_close(this->handles_[0]);
		_close(this->handles_[1]);

		if (this->console_runner_.joinable())
		{
			this->console_runner_.join();
		}
	}

	void post_load() override
	{
		if (!game::is_mp()) return;

		scheduler::frame([this]()
		{
			if (*reinterpret_cast<DWORD*>(game::native::Sys_ShowConsole) == 0x28EC8348)
			{
				this->initialize();
				return scheduler::cond_end;
			}

			return scheduler::cond_continue;
		});
	}

private:
	volatile bool console_initialized_ = false;
	volatile bool terminate_runner_ = false;

	std::mutex mutex_;
	std::thread console_runner_;
	std::queue<std::string> message_queue_;

	int handles_[2]{};

	void initialize()
	{
		std::thread([this]()
		{
			std::this_thread::sleep_for(500ms);
			game::native::Sys_ShowConsole();

			{
				std::lock_guard _(this->mutex_);
				this->console_initialized_ = true;
			}

			MSG msg;
			while (/*IsWindow(*Game::consoleWindow) != FALSE*/ true)
			{
				if (PeekMessageA(&msg, nullptr, NULL, NULL, PM_REMOVE))
				{
					if (msg.message == WM_QUIT) break;

					TranslateMessage(&msg);
					DispatchMessage(&msg);
				}
				else
				{
					//log_messages();
					std::this_thread::sleep_for(1ms);
				}
			}

			// TODO: Invoke on the main thread
			//Game::Com_Quit_f();
			exit(0);
		}).detach();
	}

	void log_messages()
	{
		while (this->console_initialized_ && !this->message_queue_.empty())
		{
			std::queue<std::string> message_queue_copy;

			{
				std::lock_guard _(this->mutex_);
				message_queue_copy = this->message_queue_;
				this->message_queue_ = {};
			}

			while (!message_queue_copy.empty())
			{
				log_message(message_queue_copy.front());
				message_queue_copy.pop();
			}
		}

		fflush(stdout);
		fflush(stderr);
	}

	static void log_message(const std::string& message)
	{
		OutputDebugStringA(message.data());
		game::native::Conbuf_AppendText(message.data());
	}

	void runner()
	{
		char buffer[1024];

		while (!this->terminate_runner_ && this->handles_[0])
		{
			const auto len = _read(this->handles_[0], buffer, sizeof(buffer));
			if (len > 0)
			{
				std::lock_guard _(this->mutex_);
				this->message_queue_.push(std::string(buffer, len));
			}
			else
			{
				std::this_thread::sleep_for(10ms);
			}
		}

		std::this_thread::yield();
	}
};

//REGISTER_MODULE(console)