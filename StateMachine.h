#pragma once

#include <functional>
#include <optional>
#include <unordered_map>
#include <cassert>

template <typename EnumClass>
class StateMachine
{
public:
    using StateType              = EnumClass;
    using ResultType             = std::optional<StateType>;
    using LeaveFunctionType      = std::function<void()>;
    using TransitionFunctionType = std::function<ResultType()>;

    StateMachine(StateType initialState, TransitionFunctionType&& enterFunc, TransitionFunctionType&& tickFunc,
                 LeaveFunctionType&& leaveFunc)
    {
        _currentState =
            &_states
                 .emplace(std::make_pair(initialState, StateMachineState(initialState, std::move(enterFunc),
                                                                         std::move(tickFunc), std::move(leaveFunc))))
                 .first->second;
    };

    void addState(StateType state, TransitionFunctionType&& enterFunc, TransitionFunctionType&& tickFunc,
                  LeaveFunctionType&& leaveFunc)
    {
        _states.emplace(std::make_pair(
            state, StateMachineState(state, std::move(enterFunc), std::move(tickFunc), std::move(leaveFunc))));
    }

    void changeState(StateType state)
    {
        assert(_currentState);
        _currentState->leaveFunc();
        auto iter = _states.find(state);
        assert(iter != _states.end());
        _currentState = &iter->second;
        ResultType transition = _currentState->enterFunc();
        if (transition)
        {
            changeState(*transition);
        }
    }

    void tick()
    {
        assert(_currentState);
        ResultType transition = _currentState->tickFunc();
        if (transition)
        {
            changeState(*transition);
        }
    }

    StateType currentState() const
    {
        assert(_currentState);
        return _currentState->state;
    }

private:
    struct StateMachineState
    {
        StateMachineState(StateType iState, TransitionFunctionType&& iEnter, TransitionFunctionType&& iTick,
                          LeaveFunctionType&& iLeave)
            : state(iState)
            , enterFunc(std::move(iEnter))
            , tickFunc(std::move(iTick))
            , leaveFunc(std::move(iLeave))
        {
        }

        StateType              state;
        TransitionFunctionType enterFunc;
        TransitionFunctionType tickFunc;
        LeaveFunctionType      leaveFunc;
    };

    StateMachineState*                               _currentState;
    std::unordered_map<StateType, StateMachineState> _states;
};

// Context variant
template <typename EnumClass, typename Context>
class StateMachineWithContext
{
public:
    using StateType              = EnumClass;
    using ContextType            = Context;
    using ResultType             = std::optional<StateType>;
    using LeaveFunctionType      = std::function<void(ContextType&)>;
    using TransitionFunctionType = std::function<ResultType(ContextType&)>;

    StateMachineWithContext(Context& context, StateType initialState, TransitionFunctionType&& enterFunc,
                            TransitionFunctionType&& tickFunc, LeaveFunctionType&& leaveFunc)
        : _context(context)
    {
        _currentState =
            &_states
                 .emplace(std::make_pair(initialState, StateMachineState(initialState, std::move(enterFunc),
                                                                         std::move(tickFunc), std::move(leaveFunc))))
                 .first->second;
    };

    void addState(StateType state, TransitionFunctionType&& enterFunc, TransitionFunctionType&& tickFunc,
                  LeaveFunctionType&& leaveFunc)
    {
        _states.emplace(std::make_pair(
            state, StateMachineState(state, std::move(enterFunc), std::move(tickFunc), std::move(leaveFunc))));
    }

    void changeState(StateType state)
    {
        assert(_currentState);
        _currentState->leaveFunc(_context);
        auto iter = _states.find(state);
        assert(iter != _states.end());
        _currentState         = &iter->second;
        ResultType transition = _currentState->enterFunc(_context);
        if (transition)
        {
            changeState(*transition);
        }
    }

    void tick()
    {
        assert(_currentState);
        ResultType transition = _currentState->tickFunc(_context);
        if (transition)
        {
            changeState(*transition);
        }
    }

    StateType currentState() const
    {
        assert(_currentState);
        return _currentState->state;
    }

private:
    struct StateMachineState
    {
        StateMachineState(StateType iState, TransitionFunctionType&& iEnter, TransitionFunctionType&& iTick,
                          LeaveFunctionType&& iLeave)
            : state(iState)
            , enterFunc(std::move(iEnter))
            , tickFunc(std::move(iTick))
            , leaveFunc(std::move(iLeave))
        {
        }

        StateType              state;
        TransitionFunctionType enterFunc;
        TransitionFunctionType tickFunc;
        LeaveFunctionType      leaveFunc;
    };

    ContextType&                                     _context;
    StateMachineState*                               _currentState;
    std::unordered_map<StateType, StateMachineState> _states;
};
