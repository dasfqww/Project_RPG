; Observe this world's Experience. No selection or feature activation is changed.
(event EventBeginPlay
  (RPG|Experience|WaitForRPGExperienceReady
    :TimeoutSeconds (Variables|Experience|GetWaitTimeout)
    (:then
      ; Activate can deliver a result synchronously before this continuation runs.
      (Utilities|FlowControl|Branch
        :Condition (Variables|Experience|GetWaitFinished)
        (:else (Variables|Experience|SetPendingWait _asynctaskproxy))))
    (:Ready
      (Variables|Experience|SetWaitFinished true)
      (Variables|Experience|SetPendingWait)
      (Variables|Experience|SetReadyExperience _experience)
      (Variables|Experience|SetLastReason "")
      (Variables|Experience|SetExperienceReady true)
      (Default|CallOnExperienceReady))
    (:Failed
      (Variables|Experience|SetWaitFinished true)
      (Variables|Experience|SetPendingWait)
      (Variables|Experience|SetReadyExperience)
      (Variables|Experience|SetLastReason _reason)
      (Variables|Experience|SetExperienceReady false)
      (Default|CallOnExperienceFailed))
    (:Cancelled
      (Variables|Experience|SetWaitFinished true)
      (Variables|Experience|SetPendingWait)
      (Variables|Experience|SetReadyExperience)
      (Variables|Experience|SetLastReason _reason)
      (Variables|Experience|SetExperienceReady false)
      (Default|CallOnExperienceCancelled))))

; Explicit Cancel removes this actor's waiter and sends no result event.
(event EventEndPlay
  (Variables|Experience|SetWaitFinished true)
  (bind pending (Variables|Experience|GetPendingWait))
  (Utilities|IsValid :InputObject pending
    (:"Is Valid"
      (AsyncAction|Cancel :self pending)
      (Variables|Experience|SetPendingWait))
    (:"Is Not Valid"
      (Variables|Experience|SetPendingWait))))
